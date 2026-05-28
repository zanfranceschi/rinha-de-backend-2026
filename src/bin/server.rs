use std::{
    env, fs,
    net::SocketAddr,
    os::unix::io::RawFd,
    path::{Path, PathBuf},
    sync::{
        Arc,
        atomic::{AtomicU64, Ordering},
    },
    time::{Duration, Instant},
};

use mimalloc::MiMalloc;
#[global_allocator]
static GLOBAL: MiMalloc = MiMalloc;

use anyhow::{Context, Result, anyhow};
use axum::{
    Json, Router,
    body::Bytes,
    extract::State,
    http::StatusCode,
    routing::{get, post},
};
use rinha_backend_2026::{
    FraudRequest, FraudResponse, deny_response, heuristic_response, load_mcc_risk,
    load_normalization, search::SearchEngine, simd, vectorize,
};
use tracing::{debug, info};

const LATENCY_BUCKET_LABELS: [&str; 11] = [
    "le_1ms",
    "le_2ms",
    "le_5ms",
    "le_10ms",
    "le_25ms",
    "le_50ms",
    "le_100ms",
    "le_250ms",
    "le_500ms",
    "le_1000ms",
    "gt_1000ms",
];
const LATENCY_BUCKET_LIMITS_US: [u128; 10] = [
    1_000, 2_000, 5_000, 10_000, 25_000, 50_000, 100_000, 250_000, 500_000, 1_000_000,
];
const SCORE_BUCKET_LABELS: [&str; 6] = ["0.0", "0.2", "0.4", "0.6", "0.8", "1.0"];

#[derive(Clone)]
struct AppState {
    engine: Arc<SearchEngine>,
    normalization: Arc<rinha_backend_2026::Normalization>,
    mcc_risk: Arc<rinha_backend_2026::MccRiskMap>,
    observability: Arc<Observability>,
}

struct Observability {
    enabled: bool,
    interval: Duration,
    requests: AtomicU64,
    approved: AtomicU64,
    denied: AtomicU64,
    json_errors: AtomicU64,
    vectorize_errors: AtomicU64,
    search_errors: AtomicU64,
    heuristic_fallbacks: AtomicU64,
    latency_buckets: [AtomicU64; LATENCY_BUCKET_LABELS.len()],
    score_buckets: [AtomicU64; SCORE_BUCKET_LABELS.len()],
}

#[derive(Clone)]
struct RuntimeInfo {
    vector_count: u64,
    cluster_count: usize,
    probe_count: usize,
    adaptive_probing_enabled: bool,
    initial_probe_count: usize,
    build_cpu_profile: String,
    target_arch: &'static str,
    avx2_enabled: bool,
    candidate_kernel: simd::KernelMode,
    centroid_kernel: simd::KernelMode,
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum ListenConfig {
    Tcp(SocketAddr),
    Unix(PathBuf),
    /// SCM_RIGHTS fd-passing: server binds a UDS control socket and receives
    /// raw TCP FDs from the LB via recvmsg. No data flows through the LB.
    FdSocket(PathBuf),
}

struct ObservabilitySnapshot {
    requests: u64,
    approved: u64,
    denied: u64,
    json_errors: u64,
    vectorize_errors: u64,
    search_errors: u64,
    heuristic_fallbacks: u64,
    latency_buckets: [u64; LATENCY_BUCKET_LABELS.len()],
    score_buckets: [u64; SCORE_BUCKET_LABELS.len()],
}

#[tokio::main(flavor = "current_thread")]
async fn main() -> Result<()> {
    init_tracing();

    let normalization_path = env_path("NORMALIZATION_PATH", "resources/normalization.json");
    let mcc_risk_path = env_path("MCC_RISK_PATH", "resources/mcc_risk.json");
    let artifact_dir = env_path("ARTIFACT_DIR", "docker/artifacts");
    let probe_override = env::var("PROBE_COUNT")
        .ok()
        .and_then(|value| value.parse().ok());
    let listen_config = ListenConfig::from_env()?;

    let normalization = Arc::new(load_normalization(&normalization_path)?);
    let mcc_risk = Arc::new(load_mcc_risk(&mcc_risk_path)?);
    let engine = SearchEngine::open(&artifact_dir, probe_override)?;
    if env_flag("LOCK_ARTIFACTS", false) {
        match engine.lock_artifacts() {
            Ok(()) => info!("locked mapped search artifacts"),
            Err(error) => info!(%error, "failed to lock mapped search artifacts"),
        }
    }
    let engine = Arc::new(engine);
    let observability = Arc::new(Observability::from_env());
    let runtime_info = RuntimeInfo {
        vector_count: engine.meta.vector_count,
        cluster_count: engine.meta.cluster_count,
        probe_count: engine.meta.probe_count,
        adaptive_probing_enabled: engine.adaptive_probing_enabled(),
        initial_probe_count: engine.initial_probe_count(),
        build_cpu_profile: env::var("BUILD_CPU_PROFILE").unwrap_or_else(|_| "local".to_string()),
        target_arch: simd::target_arch(),
        avx2_enabled: engine.avx2_enabled(),
        candidate_kernel: engine.candidate_kernel_mode(),
        centroid_kernel: engine.centroid_kernel_mode(),
    };

    info!(
        vectors = runtime_info.vector_count,
        clusters = runtime_info.cluster_count,
        probes = runtime_info.probe_count,
        adaptive_probing_enabled = runtime_info.adaptive_probing_enabled,
        initial_probes = runtime_info.initial_probe_count,
        build_cpu_profile = %runtime_info.build_cpu_profile,
        target_arch = runtime_info.target_arch,
        avx2_detected = runtime_info.avx2_enabled,
        candidate_kernel = ?runtime_info.candidate_kernel,
        centroid_kernel = ?runtime_info.centroid_kernel,
        observability_enabled = observability.enabled,
        observability_interval_secs = observability.interval.as_secs(),
        "artifacts loaded"
    );
    observability.clone().spawn_logger(runtime_info);

    let state = AppState {
        engine,
        normalization,
        mcc_risk,
        observability,
    };

    let router = Router::new()
        .route("/ready", get(ready))
        .route("/fraud-score", post(fraud_score))
        .with_state(state);

    serve(router, listen_config).await
}

impl ListenConfig {
    fn from_env() -> Result<Self> {
        Self::from_values(
            env::var("LISTEN_MODE").ok().as_deref(),
            env::var("BIND_ADDR").ok().as_deref(),
            env::var("BIND_SOCKET").ok().as_deref(),
            env::var("FD_SOCKET").ok().as_deref(),
        )
    }

    fn from_values(
        listen_mode: Option<&str>,
        bind_addr: Option<&str>,
        bind_socket: Option<&str>,
        fd_socket: Option<&str>,
    ) -> Result<Self> {
        match listen_mode.unwrap_or("tcp") {
            "tcp" => {
                let address = bind_addr.unwrap_or("0.0.0.0:9999");
                Ok(Self::Tcp(address.parse().context("invalid BIND_ADDR")?))
            }
            "unix" => {
                let socket = bind_socket
                    .filter(|value| !value.is_empty())
                    .ok_or_else(|| anyhow!("BIND_SOCKET is required when LISTEN_MODE=unix"))?;
                Ok(Self::Unix(PathBuf::from(socket)))
            }
            "fd" => {
                let socket = fd_socket
                    .or(bind_socket)
                    .filter(|value| !value.is_empty())
                    .ok_or_else(|| {
                        anyhow!("FD_SOCKET (or BIND_SOCKET) is required when LISTEN_MODE=fd")
                    })?;
                Ok(Self::FdSocket(PathBuf::from(socket)))
            }
            mode => Err(anyhow!(
                "invalid LISTEN_MODE={mode}; expected tcp, unix, or fd"
            )),
        }
    }
}

async fn serve(router: Router, listen_config: ListenConfig) -> Result<()> {
    match listen_config {
        ListenConfig::Tcp(bind_addr) => {
            info!(address = %bind_addr, listen_mode = "tcp", "listening");
            let listener = tokio::net::TcpListener::bind(bind_addr).await?;
            axum::serve(listener, router)
                .with_graceful_shutdown(shutdown_signal())
                .await
                .context("server exited unexpectedly")
        }
        ListenConfig::Unix(bind_socket) => serve_unix(router, bind_socket).await,
        ListenConfig::FdSocket(fd_socket) => serve_fd_socket(router, fd_socket).await,
    }
}

#[cfg(unix)]
async fn serve_unix(router: Router, bind_socket: PathBuf) -> Result<()> {
    use std::os::unix::fs::PermissionsExt;

    prepare_unix_socket(&bind_socket)?;
    let listener = tokio::net::UnixListener::bind(&bind_socket)
        .with_context(|| format!("failed to bind {}", bind_socket.display()))?;
    fs::set_permissions(&bind_socket, fs::Permissions::from_mode(0o666))
        .with_context(|| format!("failed to chmod {}", bind_socket.display()))?;

    info!(socket = %bind_socket.display(), listen_mode = "unix", "listening");
    axum::serve(listener, router)
        .with_graceful_shutdown(shutdown_signal())
        .await
        .context("server exited unexpectedly")
}

#[cfg(not(unix))]
async fn serve_unix(_router: Router, bind_socket: PathBuf) -> Result<()> {
    Err(anyhow!(
        "LISTEN_MODE=unix is not supported on this platform: {}",
        bind_socket.display()
    ))
}

/// SCM_RIGHTS fd-passing mode: bind a UDS control socket, receive raw TCP FDs
/// from the LB via recvmsg(SCM_RIGHTS), and serve each one directly through hyper.
/// The LB never copies any request/response bytes — it just passes socket ownership.
#[cfg(unix)]
async fn serve_fd_socket(router: Router, fd_socket: PathBuf) -> Result<()> {
    use std::os::unix::fs::PermissionsExt;
    use std::os::unix::io::FromRawFd;

    use hyper::server::conn::http1;
    use hyper::service::service_fn;
    use hyper_util::rt::TokioIo;
    use tower::Service;

    prepare_unix_socket(&fd_socket)?;
    let listener = tokio::net::UnixListener::bind(&fd_socket)
        .with_context(|| format!("failed to bind fd socket {}", fd_socket.display()))?;
    fs::set_permissions(&fd_socket, fs::Permissions::from_mode(0o666))
        .with_context(|| format!("failed to chmod {}", fd_socket.display()))?;

    info!(socket = %fd_socket.display(), listen_mode = "fd", "listening");

    let (fd_tx, mut fd_rx) = tokio::sync::mpsc::unbounded_channel::<RawFd>();

    // Accept LB control connections and receive FDs in blocking threads.
    let accept_task = {
        let fd_tx = fd_tx.clone();
        tokio::spawn(async move {
            loop {
                match listener.accept().await {
                    Ok((control, _)) => {
                        let fd_tx = fd_tx.clone();
                        // recvmsg is blocking; run it off the async executor.
                        tokio::task::spawn_blocking(move || {
                            let std_control = control.into_std().unwrap();
                            // into_std() leaves the fd in non-blocking mode; recvmsg
                            // must block or it returns EAGAIN and the loop exits immediately.
                            let _ = std_control.set_nonblocking(false);
                            loop {
                                match recv_fd_blocking(&std_control) {
                                    Some(fd) => {
                                        if fd_tx.send(fd).is_err() {
                                            break;
                                        }
                                    }
                                    None => break,
                                }
                            }
                        });
                    }
                    Err(e) => {
                        eprintln!("fd control accept error: {}", e);
                    }
                }
            }
        })
    };

    // Serve received FDs through hyper.
    loop {
        tokio::select! {
            Some(fd) = fd_rx.recv() => {
                // Set TCP_NODELAY and (Linux) TCP_QUICKACK before handing to tokio.
                unsafe {
                    let one: libc::c_int = 1;
                    libc::setsockopt(
                        fd, libc::IPPROTO_TCP, libc::TCP_NODELAY,
                        &one as *const _ as *const _,
                        std::mem::size_of::<libc::c_int>() as libc::socklen_t,
                    );
                    #[cfg(target_os = "linux")]
                    libc::setsockopt(
                        fd, libc::IPPROTO_TCP, libc::TCP_QUICKACK,
                        &one as *const _ as *const _,
                        std::mem::size_of::<libc::c_int>() as libc::socklen_t,
                    );
                }

                let std_stream = unsafe { std::net::TcpStream::from_raw_fd(fd) };
                if let Err(e) = std_stream.set_nonblocking(true) {
                    eprintln!("set_nonblocking failed for fd {}: {}", fd, e);
                    continue;
                }
                let tokio_stream = match tokio::net::TcpStream::from_std(std_stream) {
                    Ok(s) => s,
                    Err(e) => {
                        eprintln!("failed to register fd {} with tokio: {}", fd, e);
                        continue;
                    }
                };

                let router = router.clone();
                tokio::spawn(async move {
                    let io = TokioIo::new(tokio_stream);
                    let svc = service_fn(move |req| {
                        let mut r = router.clone();
                        async move { r.call(req).await }
                    });
                    let _ = http1::Builder::new()
                        .keep_alive(true)
                        .serve_connection(io, svc)
                        .await;
                });
            }
            _ = shutdown_signal() => {
                accept_task.abort();
                break;
            }
        }
    }

    Ok(())
}

#[cfg(not(unix))]
async fn serve_fd_socket(_router: Router, fd_socket: PathBuf) -> Result<()> {
    Err(anyhow!(
        "LISTEN_MODE=fd is not supported on this platform: {}",
        fd_socket.display()
    ))
}

/// Blocking recvmsg call to extract one FD from a UDS control stream.
/// Returns None on EOF or unrecoverable error.
#[cfg(unix)]
fn recv_fd_blocking(stream: &std::os::unix::net::UnixStream) -> Option<RawFd> {
    let mut payload: u8 = 0;
    let mut iov = libc::iovec {
        iov_base: &mut payload as *mut _ as *mut _,
        iov_len: 1,
    };
    let mut cmsg_buf = [0u8; 64];
    let mut msg: libc::msghdr = unsafe { std::mem::zeroed() };
    msg.msg_iov = &mut iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf.as_mut_ptr() as *mut _;
    msg.msg_controllen = cmsg_buf.len() as _;

    use std::os::unix::io::AsRawFd;
    loop {
        let n = unsafe { libc::recvmsg(stream.as_raw_fd(), &mut msg, 0) };
        if n < 0 {
            let e = std::io::Error::last_os_error();
            if e.raw_os_error() == Some(libc::EINTR) {
                continue;
            }
            return None;
        }
        if n == 0 {
            return None;
        }
        unsafe {
            let mut cmsg = libc::CMSG_FIRSTHDR(&msg);
            while !cmsg.is_null() {
                if (*cmsg).cmsg_level == libc::SOL_SOCKET
                    && (*cmsg).cmsg_type == libc::SCM_RIGHTS
                    && (*cmsg).cmsg_len
                        >= libc::CMSG_LEN(std::mem::size_of::<libc::c_int>() as u32) as _
                {
                    let mut fd: libc::c_int = -1;
                    std::ptr::copy_nonoverlapping(
                        libc::CMSG_DATA(cmsg) as *const u8,
                        &mut fd as *mut libc::c_int as *mut u8,
                        std::mem::size_of::<libc::c_int>(),
                    );
                    return Some(fd);
                }
                cmsg = libc::CMSG_NXTHDR(&msg, cmsg);
            }
        }
    }
}

#[cfg(unix)]
fn prepare_unix_socket(path: &Path) -> Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .with_context(|| format!("failed to create {}", parent.display()))?;
    }

    match fs::remove_file(path) {
        Ok(()) => Ok(()),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(error).with_context(|| format!("failed to remove {}", path.display())),
    }
}

async fn ready() -> StatusCode {
    StatusCode::OK
}

async fn fraud_score(
    State(state): State<AppState>,
    body: Bytes,
) -> (StatusCode, Json<FraudResponse>) {
    let started_at = Instant::now();
    let request: FraudRequest<'_> = match serde_json::from_slice(&body) {
        Ok(request) => request,
        Err(error) => {
            debug!(%error, "failed to deserialize request");
            state.observability.record_json_error();
            let response = deny_response();
            state
                .observability
                .record_response(&response, started_at.elapsed());
            return (StatusCode::BAD_REQUEST, Json(response));
        }
    };

    let response = match vectorize(&request, &state.normalization, &state.mcc_risk) {
        Ok(vector) => match state.engine.score(&vector) {
            Ok(response) => response,
            Err(error) => {
                debug!(%error, tx_id = %request.id, "search failed, using heuristic fallback");
                state.observability.record_search_error();
                state.observability.record_heuristic_fallback();
                heuristic_response(&request, &state.normalization, &state.mcc_risk)
            }
        },
        Err(error) => {
            debug!(%error, tx_id = %request.id, "invalid request payload");
            state.observability.record_vectorize_error();
            let response = deny_response();
            state
                .observability
                .record_response(&response, started_at.elapsed());
            return (StatusCode::BAD_REQUEST, Json(response));
        }
    };
    state
        .observability
        .record_response(&response, started_at.elapsed());

    (StatusCode::OK, Json(response))
}

impl Observability {
    fn from_env() -> Self {
        Self {
            enabled: env_flag("OBSERVABILITY", false),
            interval: env_duration("OBS_INTERVAL_SECS", 5),
            requests: AtomicU64::new(0),
            approved: AtomicU64::new(0),
            denied: AtomicU64::new(0),
            json_errors: AtomicU64::new(0),
            vectorize_errors: AtomicU64::new(0),
            search_errors: AtomicU64::new(0),
            heuristic_fallbacks: AtomicU64::new(0),
            latency_buckets: std::array::from_fn(|_| AtomicU64::new(0)),
            score_buckets: std::array::from_fn(|_| AtomicU64::new(0)),
        }
    }

    fn spawn_logger(self: Arc<Self>, runtime_info: RuntimeInfo) {
        if !self.enabled {
            return;
        }

        std::thread::Builder::new()
            .name("observability-logger".to_string())
            .spawn(move || {
                let mut previous = self.snapshot();
                loop {
                    let started_at = Instant::now();
                    std::thread::sleep(self.interval);
                    let elapsed = started_at.elapsed();
                    let current = self.snapshot();
                    let delta = current.saturating_sub(&previous);
                    previous = current;
                    log_observability_interval(&delta, elapsed, &runtime_info);
                }
            })
            .expect("failed to spawn observability logger");
    }

    fn record_json_error(&self) {
        if self.enabled {
            self.json_errors.fetch_add(1, Ordering::Relaxed);
        }
    }

    fn record_vectorize_error(&self) {
        if self.enabled {
            self.vectorize_errors.fetch_add(1, Ordering::Relaxed);
        }
    }

    fn record_search_error(&self) {
        if self.enabled {
            self.search_errors.fetch_add(1, Ordering::Relaxed);
        }
    }

    fn record_heuristic_fallback(&self) {
        if self.enabled {
            self.heuristic_fallbacks.fetch_add(1, Ordering::Relaxed);
        }
    }

    fn record_response(&self, response: &FraudResponse, elapsed: Duration) {
        if !self.enabled {
            return;
        }

        if response.approved {
            self.approved.fetch_add(1, Ordering::Relaxed);
        } else {
            self.denied.fetch_add(1, Ordering::Relaxed);
        }

        self.latency_buckets[latency_bucket(elapsed)].fetch_add(1, Ordering::Relaxed);
        self.score_buckets[score_bucket(response.fraud_score)].fetch_add(1, Ordering::Relaxed);
        self.requests.fetch_add(1, Ordering::Relaxed);
    }

    fn snapshot(&self) -> ObservabilitySnapshot {
        ObservabilitySnapshot {
            requests: self.requests.load(Ordering::Relaxed),
            approved: self.approved.load(Ordering::Relaxed),
            denied: self.denied.load(Ordering::Relaxed),
            json_errors: self.json_errors.load(Ordering::Relaxed),
            vectorize_errors: self.vectorize_errors.load(Ordering::Relaxed),
            search_errors: self.search_errors.load(Ordering::Relaxed),
            heuristic_fallbacks: self.heuristic_fallbacks.load(Ordering::Relaxed),
            latency_buckets: std::array::from_fn(|idx| {
                self.latency_buckets[idx].load(Ordering::Relaxed)
            }),
            score_buckets: std::array::from_fn(|idx| {
                self.score_buckets[idx].load(Ordering::Relaxed)
            }),
        }
    }
}

impl ObservabilitySnapshot {
    fn saturating_sub(&self, previous: &Self) -> Self {
        Self {
            requests: self.requests.saturating_sub(previous.requests),
            approved: self.approved.saturating_sub(previous.approved),
            denied: self.denied.saturating_sub(previous.denied),
            json_errors: self.json_errors.saturating_sub(previous.json_errors),
            vectorize_errors: self
                .vectorize_errors
                .saturating_sub(previous.vectorize_errors),
            search_errors: self.search_errors.saturating_sub(previous.search_errors),
            heuristic_fallbacks: self
                .heuristic_fallbacks
                .saturating_sub(previous.heuristic_fallbacks),
            latency_buckets: std::array::from_fn(|idx| {
                self.latency_buckets[idx].saturating_sub(previous.latency_buckets[idx])
            }),
            score_buckets: std::array::from_fn(|idx| {
                self.score_buckets[idx].saturating_sub(previous.score_buckets[idx])
            }),
        }
    }
}

fn log_observability_interval(
    delta: &ObservabilitySnapshot,
    elapsed: Duration,
    runtime_info: &RuntimeInfo,
) {
    let elapsed_secs = elapsed.as_secs_f64().max(0.001);
    let rps = delta.requests as f64 / elapsed_secs;
    let fallback_rate = rate(delta.heuristic_fallbacks, delta.requests);
    let json_error_rate = rate(delta.json_errors, delta.requests);
    let search_error_rate = rate(delta.search_errors, delta.requests);
    let vectorize_error_rate = rate(delta.vectorize_errors, delta.requests);
    let p99_bucket = percentile_bucket(&delta.latency_buckets, 0.99);
    let latency_buckets = format_buckets(&LATENCY_BUCKET_LABELS, &delta.latency_buckets);
    let fraud_score_buckets = format_buckets(&SCORE_BUCKET_LABELS, &delta.score_buckets);

    info!(
        interval_secs = elapsed_secs,
        requests = delta.requests,
        rps,
        approved = delta.approved,
        denied = delta.denied,
        json_errors = delta.json_errors,
        vectorize_errors = delta.vectorize_errors,
        search_errors = delta.search_errors,
        heuristic_fallbacks = delta.heuristic_fallbacks,
        fallback_rate,
        json_error_rate,
        vectorize_error_rate,
        search_error_rate,
        p99_bucket,
        latency_buckets = %latency_buckets,
        fraud_score_buckets = %fraud_score_buckets,
        vectors = runtime_info.vector_count,
        clusters = runtime_info.cluster_count,
        probes = runtime_info.probe_count,
        adaptive_probing_enabled = runtime_info.adaptive_probing_enabled,
        initial_probes = runtime_info.initial_probe_count,
        build_cpu_profile = %runtime_info.build_cpu_profile,
        target_arch = runtime_info.target_arch,
        avx2_detected = runtime_info.avx2_enabled,
        candidate_kernel = ?runtime_info.candidate_kernel,
        centroid_kernel = ?runtime_info.centroid_kernel,
        "observability interval"
    );
}

fn latency_bucket(elapsed: Duration) -> usize {
    let micros = elapsed.as_micros();
    LATENCY_BUCKET_LIMITS_US
        .iter()
        .position(|limit| micros <= *limit)
        .unwrap_or(LATENCY_BUCKET_LABELS.len() - 1)
}

fn score_bucket(score: f32) -> usize {
    ((score.clamp(0.0, 1.0) * 5.0).round() as usize).min(SCORE_BUCKET_LABELS.len() - 1)
}

fn percentile_bucket(
    buckets: &[u64; LATENCY_BUCKET_LABELS.len()],
    percentile: f64,
) -> &'static str {
    let total: u64 = buckets.iter().sum();
    if total == 0 {
        return "none";
    }

    let threshold = ((total as f64) * percentile).ceil() as u64;
    let mut cumulative = 0_u64;
    for (idx, count) in buckets.iter().enumerate() {
        cumulative += *count;
        if cumulative >= threshold {
            return LATENCY_BUCKET_LABELS[idx];
        }
    }
    LATENCY_BUCKET_LABELS[LATENCY_BUCKET_LABELS.len() - 1]
}

fn format_buckets<const N: usize>(labels: &[&str; N], buckets: &[u64; N]) -> String {
    labels
        .iter()
        .zip(buckets.iter())
        .map(|(label, value)| format!("{label}={value}"))
        .collect::<Vec<_>>()
        .join(",")
}

fn rate(count: u64, total: u64) -> f64 {
    if total == 0 {
        0.0
    } else {
        count as f64 / total as f64
    }
}

fn env_flag(key: &str, default: bool) -> bool {
    env::var(key)
        .ok()
        .map(|value| {
            matches!(
                value.as_str(),
                "1" | "true" | "TRUE" | "yes" | "YES" | "on" | "ON"
            )
        })
        .unwrap_or(default)
}

fn env_duration(key: &str, default_secs: u64) -> Duration {
    let secs = env::var(key)
        .ok()
        .and_then(|value| value.parse::<u64>().ok())
        .unwrap_or(default_secs)
        .max(1);
    Duration::from_secs(secs)
}

fn env_path(key: &str, default: &str) -> PathBuf {
    env::var(key)
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from(default))
}

fn init_tracing() {
    let _ = tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "info,rinha_backend_2026=info".into()),
        )
        .with_target(false)
        .try_init();
}

async fn shutdown_signal() {
    let ctrl_c = async {
        let _ = tokio::signal::ctrl_c().await;
    };

    #[cfg(unix)]
    let terminate = async {
        use tokio::signal::unix::{SignalKind, signal};
        if let Ok(mut stream) = signal(SignalKind::terminate()) {
            let _ = stream.recv().await;
        }
    };

    #[cfg(not(unix))]
    let terminate = std::future::pending::<()>();

    tokio::select! {
        _ = ctrl_c => {},
        _ = terminate => {},
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn latency_bucket_uses_expected_boundaries() {
        assert_eq!(latency_bucket(Duration::from_micros(999)), 0);
        assert_eq!(latency_bucket(Duration::from_millis(1)), 0);
        assert_eq!(latency_bucket(Duration::from_micros(1_001)), 1);
        assert_eq!(latency_bucket(Duration::from_secs(2)), 10);
    }

    #[test]
    fn score_bucket_matches_top_k_scores() {
        assert_eq!(score_bucket(0.0), 0);
        assert_eq!(score_bucket(0.19), 1);
        assert_eq!(score_bucket(0.4), 2);
        assert_eq!(score_bucket(0.6), 3);
        assert_eq!(score_bucket(0.8), 4);
        assert_eq!(score_bucket(1.0), 5);
    }

    #[test]
    fn percentile_bucket_returns_none_for_empty_interval() {
        assert_eq!(
            percentile_bucket(&[0; LATENCY_BUCKET_LABELS.len()], 0.99),
            "none"
        );
    }

    #[test]
    fn listen_config_defaults_to_tcp_addr() {
        assert_eq!(
            ListenConfig::from_values(None, None, None, None).unwrap(),
            ListenConfig::Tcp("0.0.0.0:9999".parse().unwrap())
        );
    }

    #[test]
    fn listen_config_accepts_custom_tcp_addr() {
        assert_eq!(
            ListenConfig::from_values(Some("tcp"), Some("127.0.0.1:8080"), None, None).unwrap(),
            ListenConfig::Tcp("127.0.0.1:8080".parse().unwrap())
        );
    }

    #[test]
    fn listen_config_requires_socket_for_unix_mode() {
        let error = ListenConfig::from_values(Some("unix"), None, None, None).unwrap_err();
        assert!(
            error
                .to_string()
                .contains("BIND_SOCKET is required when LISTEN_MODE=unix")
        );
    }

    #[test]
    fn listen_config_accepts_unix_socket() {
        assert_eq!(
            ListenConfig::from_values(Some("unix"), None, Some("/sockets/api1.sock"), None)
                .unwrap(),
            ListenConfig::Unix(PathBuf::from("/sockets/api1.sock"))
        );
    }

    #[test]
    fn listen_config_rejects_unknown_mode() {
        let error = ListenConfig::from_values(Some("udp"), None, None, None).unwrap_err();
        assert!(error.to_string().contains("invalid LISTEN_MODE=udp"));
    }

    #[test]
    fn listen_config_accepts_fd_socket() {
        assert_eq!(
            ListenConfig::from_values(Some("fd"), None, None, Some("/sockets/api1.ctrl"))
                .unwrap(),
            ListenConfig::FdSocket(PathBuf::from("/sockets/api1.ctrl"))
        );
    }

    #[test]
    fn listen_config_fd_falls_back_to_bind_socket() {
        assert_eq!(
            ListenConfig::from_values(Some("fd"), None, Some("/sockets/api1.ctrl"), None)
                .unwrap(),
            ListenConfig::FdSocket(PathBuf::from("/sockets/api1.ctrl"))
        );
    }

    #[test]
    fn listen_config_requires_socket_for_fd_mode() {
        let error = ListenConfig::from_values(Some("fd"), None, None, None).unwrap_err();
        assert!(error
            .to_string()
            .contains("FD_SOCKET (or BIND_SOCKET) is required when LISTEN_MODE=fd"));
    }
}
