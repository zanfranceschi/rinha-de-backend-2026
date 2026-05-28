//! SCM_RIGHTS fd-passing load balancer for Rinha de Backend 2026.
//!
//! For each accepted TCP connection:
//!   1. Set TCP_NODELAY on the accepted socket.
//!   2. Round-robin pick an upstream.
//!   3. sendmsg(SCM_RIGHTS) hands the raw FD over a persistent UDS control socket.
//!   4. Close the local copy — the backend now owns the socket entirely.
//!
//! Zero bytes of request/response data ever pass through this process.
//!
//! Env vars:
//!   LB_LISTEN    - TCP address to listen on (default "0.0.0.0:9999")
//!   FD_UPSTREAMS - comma-separated UDS control socket paths
//!                  (default "/sockets/api1.ctrl,/sockets/api2.ctrl")
//!   LB_WORKERS   - number of SO_REUSEPORT accept threads (default 2)

#[cfg(not(target_os = "linux"))]
fn main() {
    panic!("lb requires Linux (SCM_RIGHTS / SO_REUSEPORT)");
}

#[cfg(target_os = "linux")]
fn main() {
    use std::sync::{Arc, atomic::AtomicUsize};

    let port: u16 = std::env::var("LB_LISTEN")
        .ok()
        .and_then(|s| s.split(':').last()?.parse().ok())
        .unwrap_or(9999);

    let upstreams_raw = std::env::var("FD_UPSTREAMS")
        .unwrap_or_else(|_| "/sockets/api1.ctrl,/sockets/api2.ctrl".into());
    let upstreams: Vec<String> = upstreams_raw
        .split(',')
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .collect();

    if upstreams.is_empty() {
        eprintln!("[lb] FD_UPSTREAMS must contain at least one path");
        std::process::exit(1);
    }

    let workers: usize = std::env::var("LB_WORKERS")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(2);

    let listener_fd = create_tcp_listener(port).unwrap_or_else(|e| {
        eprintln!("[lb] failed to bind port {}: {}", port, e);
        std::process::exit(1);
    });

    eprintln!(
        "[lb] listening port={} workers={} upstreams={}",
        port,
        workers,
        upstreams.len()
    );

    wait_upstreams(&upstreams);
    eprintln!("[lb] upstreams ready");

    let upstreams = Arc::new(upstreams);
    let next = Arc::new(AtomicUsize::new(0));

    let mut handles = Vec::with_capacity(workers);
    for _ in 0..workers {
        let upstreams = upstreams.clone();
        let next = next.clone();
        handles.push(std::thread::spawn(move || {
            worker_loop(listener_fd, &upstreams, &next)
        }));
    }
    for h in handles {
        let _ = h.join();
    }
}

#[cfg(target_os = "linux")]
fn create_tcp_listener(port: u16) -> std::io::Result<libc::c_int> {
    use std::io;
    let sock = unsafe {
        libc::socket(libc::AF_INET, libc::SOCK_STREAM | libc::SOCK_CLOEXEC, 0)
    };
    if sock < 0 {
        return Err(io::Error::last_os_error());
    }
    let one: libc::c_int = 1;
    unsafe {
        libc::setsockopt(
            sock, libc::SOL_SOCKET, libc::SO_REUSEADDR,
            &one as *const _ as *const _, std::mem::size_of::<libc::c_int>() as _,
        );
        libc::setsockopt(
            sock, libc::SOL_SOCKET, libc::SO_REUSEPORT,
            &one as *const _ as *const _, std::mem::size_of::<libc::c_int>() as _,
        );
    }
    let mut addr: libc::sockaddr_in = unsafe { std::mem::zeroed() };
    addr.sin_family = libc::AF_INET as libc::sa_family_t;
    addr.sin_addr.s_addr = u32::to_be(libc::INADDR_ANY);
    addr.sin_port = u16::to_be(port);
    let r = unsafe {
        libc::bind(
            sock,
            &addr as *const _ as *const _,
            std::mem::size_of::<libc::sockaddr_in>() as _,
        )
    };
    if r != 0 {
        let e = std::io::Error::last_os_error();
        unsafe { libc::close(sock) };
        return Err(e);
    }
    if unsafe { libc::listen(sock, 8192) } != 0 {
        let e = std::io::Error::last_os_error();
        unsafe { libc::close(sock) };
        return Err(e);
    }
    Ok(sock)
}

#[cfg(target_os = "linux")]
fn connect_unix(path: &str) -> std::io::Result<libc::c_int> {
    use std::io;
    let fd = unsafe {
        libc::socket(libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_CLOEXEC, 0)
    };
    if fd < 0 {
        return Err(io::Error::last_os_error());
    }
    let mut addr: libc::sockaddr_un = unsafe { std::mem::zeroed() };
    addr.sun_family = libc::AF_UNIX as libc::sa_family_t;
    let bytes = path.as_bytes();
    if bytes.len() >= addr.sun_path.len() {
        unsafe { libc::close(fd) };
        return Err(io::Error::new(io::ErrorKind::InvalidInput, "path too long"));
    }
    for (i, &b) in bytes.iter().enumerate() {
        addr.sun_path[i] = b as libc::c_char;
    }
    let r = unsafe {
        libc::connect(
            fd,
            &addr as *const _ as *const _,
            std::mem::size_of::<libc::sockaddr_un>() as _,
        )
    };
    if r != 0 {
        let e = io::Error::last_os_error();
        unsafe { libc::close(fd) };
        return Err(e);
    }
    Ok(fd)
}

#[cfg(target_os = "linux")]
fn wait_upstreams(upstreams: &[String]) {
    let delay = std::time::Duration::from_millis(50);
    for _ in 0..200 {
        let ready = upstreams
            .iter()
            .filter(|p| match connect_unix(p) {
                Ok(fd) => {
                    unsafe { libc::close(fd) };
                    true
                }
                Err(_) => false,
            })
            .count();
        if ready == upstreams.len() {
            return;
        }
        std::thread::sleep(delay);
    }
    eprintln!("[lb] upstreams not all ready after 10s, proceeding anyway");
}

#[cfg(target_os = "linux")]
fn set_tcp_nodelay(fd: libc::c_int) {
    let one: libc::c_int = 1;
    unsafe {
        libc::setsockopt(
            fd, libc::IPPROTO_TCP, libc::TCP_NODELAY,
            &one as *const _ as *const _, std::mem::size_of::<libc::c_int>() as _,
        );
    }
}

/// Sends client_fd to the backend via an already-open UDS control socket.
#[cfg(target_os = "linux")]
fn send_fd_once(control_fd: libc::c_int, client_fd: libc::c_int) -> bool {
    let payload: u8 = 0;
    let mut iov = libc::iovec {
        iov_base: &payload as *const _ as *mut _,
        iov_len: 1,
    };
    // CMSG_SPACE(sizeof(c_int)) == 16 on x86_64 Linux; use 32 for alignment safety.
    let mut cmsg_buf = [0u8; 32];
    let mut msg: libc::msghdr = unsafe { std::mem::zeroed() };
    msg.msg_iov = &mut iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf.as_mut_ptr() as *mut _;
    msg.msg_controllen = cmsg_buf.len() as _;
    unsafe {
        let cmsg = libc::CMSG_FIRSTHDR(&msg);
        if cmsg.is_null() {
            return false;
        }
        (*cmsg).cmsg_level = libc::SOL_SOCKET;
        (*cmsg).cmsg_type = libc::SCM_RIGHTS;
        (*cmsg).cmsg_len =
            libc::CMSG_LEN(std::mem::size_of::<libc::c_int>() as u32) as _;
        std::ptr::copy_nonoverlapping(
            &client_fd as *const libc::c_int as *const u8,
            libc::CMSG_DATA(cmsg),
            std::mem::size_of::<libc::c_int>(),
        );
        msg.msg_controllen = (*cmsg).cmsg_len;
        loop {
            let n = libc::sendmsg(control_fd, &msg, libc::MSG_NOSIGNAL);
            if n == 1 {
                return true;
            }
            if n < 0 {
                let e = std::io::Error::last_os_error();
                if e.raw_os_error() == Some(libc::EINTR) {
                    continue;
                }
                eprintln!("[lb] sendmsg failed: {}", e);
            }
            return false;
        }
    }
}

/// Persistent connection to one upstream's UDS control socket.
/// Reconnects once on failure before giving up on a given send.
#[cfg(target_os = "linux")]
struct Sender {
    path: String,
    fd: libc::c_int,
}

#[cfg(target_os = "linux")]
impl Sender {
    fn new(path: String) -> Self {
        Self { path, fd: -1 }
    }

    fn ensure_connected(&mut self) -> bool {
        if self.fd >= 0 {
            return true;
        }
        match connect_unix(&self.path) {
            Ok(fd) => {
                self.fd = fd;
                true
            }
            Err(e) => {
                eprintln!("[lb] connect {} failed: {}", self.path, e);
                false
            }
        }
    }

    fn send(&mut self, client_fd: libc::c_int) -> bool {
        if !self.ensure_connected() {
            return false;
        }
        if send_fd_once(self.fd, client_fd) {
            return true;
        }
        // Stale connection — reconnect once and retry.
        unsafe { libc::close(self.fd) };
        self.fd = -1;
        if !self.ensure_connected() {
            return false;
        }
        send_fd_once(self.fd, client_fd)
    }
}

#[cfg(target_os = "linux")]
impl Drop for Sender {
    fn drop(&mut self) {
        if self.fd >= 0 {
            unsafe { libc::close(self.fd) };
        }
    }
}

#[cfg(target_os = "linux")]
fn worker_loop(
    listener_fd: libc::c_int,
    upstreams: &[String],
    next: &std::sync::atomic::AtomicUsize,
) {
    use std::sync::atomic::Ordering;

    let mut senders: Vec<Sender> = upstreams
        .iter()
        .map(|p| Sender::new(p.clone()))
        .collect();

    loop {
        let client = unsafe {
            libc::accept4(
                listener_fd,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                libc::SOCK_CLOEXEC,
            )
        };
        if client < 0 {
            let err = std::io::Error::last_os_error();
            if err.raw_os_error() == Some(libc::EINTR) {
                continue;
            }
            eprintln!("[lb] accept error: {}", err);
            continue;
        }

        set_tcp_nodelay(client);

        let start = next.fetch_add(1, Ordering::Relaxed);
        let mut sent = false;
        for i in 0..senders.len() {
            let idx = (start + i) % senders.len();
            if senders[idx].send(client) {
                sent = true;
                break;
            }
        }

        if !sent {
            let resp =
                b"HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            unsafe {
                libc::write(client, resp.as_ptr() as *const _, resp.len());
            }
        }

        unsafe { libc::close(client) };
    }
}
