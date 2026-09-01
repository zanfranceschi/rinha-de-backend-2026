### Plano de Implementação: Detecção de Fraude por Busca Vetorial

### 1. Definição da Arquitetura
A solução seguirá a arquitetura MVC (Model-View-Controller) conforme solicitado, operando sob as restrições de infraestrutura da Rinha (1 CPU e 350MB RAM totais).

#### Estrutura de Pastas Recomendada
Para organizar a arquitetura MVC de forma clara e seguindo as convenções do Spring Boot/Kotlin, a seguinte estrutura de pacotes deve ser adotada:

```text
src/main/kotlin/com/br/rinhabackend/frauddetection/
├── config/             # Configurações do Spring (Security, Jackson, etc)
├── controller/         # Endpoints REST (Controller)
├── domain/             # Entidades de negócio e objetos de domínio
│   ├── model/          # Classes de representação de dados (DTOs/Vectors)
│   └── service/        # Lógica de detecção de fraude e normalização
├── infrastructure/     # Acesso a dados e recursos externos
│   ├── repository/     # Carregamento e acesso aos vetores (In-memory)
│   └── external/       # Clientes para leitura de JSON/GZ
└── shared/             # Utilitários e extensões comuns
```

- **Load Balancer (Nginx):** Atuará na porta `9999` distribuindo requisições em Round-Robin para as instâncias de API.
- **API Instances (Spring Boot):** Duas instâncias de API para garantir alta disponibilidade e atender ao requisito mínimo da topologia.
- **Camadas:**
    - **Controller:** Gerenciamento dos endpoints HTTP.
    - **Service:** Lógica de negócio (vetorização, normalização e cálculo de score).
    - **Repository/Data Source:** Acesso ao dataset de referência (carregado em memória para performance, dada a restrição de força bruta).

### 2. Modelos de Entidade

#### Camada de Dados (DTOs de entrada e saída)
- `TransactionRequest`: Payload recebido com dados da transação, cliente, comerciante e terminal.
- `FraudResponse`: Objeto com `approved` (boolean) e `fraud_score` (double).
- `ReferenceVector`: Estrutura para armazenar o vetor de 14 posições e seu rótulo (`fraud` ou `legit`).

#### Camada de Negócio
- `TransactionVector`: Representação das 14 dimensões após normalização.
- `NormalizationConfig`: Constantes lidas de `normalization.json`.
- `MccRiskConfig`: Mapa de riscos lido de `mcc_risk.json`.

### 3. Definição da Controller
A controller deve expor dois endpoints principais:

```kotlin
@RestController
class FraudDetectionController(val fraudService: FraudService) {

    @GetMapping("/ready")
    fun ready(): ResponseEntity<Void> {
        return if (fraudService.isReady()) {
            ResponseEntity.ok().build()
        } else {
            ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE).build()
        }
    }

    @PostMapping("/fraud-score")
    fun calculateScore(@RequestBody request: TransactionRequest): ResponseEntity<FraudResponse> {
        val response = fraudService.analyze(request)
        return ResponseEntity.ok(response)
    }
}
```

### 4. Configuração da Imagem Docker
Considerando a restrição de memória (350MB total para todos os serviços), utilizaremos o **Eclipse Temurin JRE** (mais leve) e um **Multi-stage build**.

```dockerfile
# Estágio de Build
FROM eclipse-temurin:21-jdk-jammy AS build
WORKDIR /app
COPY . .
RUN ./gradlew bootJar --no-daemon

# Estágio de Execução
FROM eclipse-temurin:21-jre-jammy
WORKDIR /app
COPY --from:build /app/build/libs/*.jar app.jar
# Garantir que as imagens sejam amd64 no build final
# Restrição de memória JVM para caber no limite do docker-compose
ENTRYPOINT ["java", "-Xmx128m", "-Xms128m", "-jar", "app.jar"]
```

### 5. Sugestões de Banco de Dados
Dada a restrição de **usar força bruta para olhar para todas as transações** e o limite de memória baixíssimo (350MB para LB + 2 APIs + DB), a abordagem mais eficiente é:

1. **In-Memory Data Structure:** Armazenar os 3 milhões de vetores diretamente na memória das instâncias de API como um array de `float[]`. 
   - *Cálculo:* 3M registros * 14 floats * 4 bytes ≈ 168 MB. Isso cabe no limite se otimizado.
2. **DuckDB ou SQLite (com extensões):** Se a memória for um impeditivo crítico para manter em Java Objects, usar um banco embarcado que gerencie o buffer pool eficientemente.
3. **PGVector (PostgreSQL):** Opção robusta, mas consome mais memória que o permitido no contexto da rinha (o Postgres sozinho pode passar de 100MB).

### 6. Dependências Necessárias (Spring Boot)
No `build.gradle.kts`:
- `spring-boot-starter-webmvc`: Para a API REST.
- `jackson-module-kotlin`: Serialização JSON.
- `kotlinx-coroutines-core`: Para processamento paralelo opcional da força bruta.
- `commons-compress`: Para ler o `references.json.gz` no startup.
- `fastutil` ou `ND4J`: Bibliotecas de coleções primitivas para economizar memória Java ao armazenar os vetores (altamente recomendado).

### 7. Abordagens para Solução

- **Pré-processamento no Startup:** Descomprimir `references.json.gz` e carregar os vetores em um array de primitivos (`float[]` unidimensional para evitar overhead de objetos) logo no início da aplicação.
- **Cálculo de Distância (Força Bruta):**
    - Implementar a **Distância Euclidiana** comparando o vetor de entrada contra todos os 3 milhões de vetores.
    - Utilizar loops altamente otimizados ou operações de vetorização da JVM (Vector API, se disponível no Java 21).
    - Para performance, manter apenas o `Top 5` menores distâncias durante o loop de varredura.
- **Gerenciamento de Memória:** 
    - Sugestão de distribuição: Nginx (10MB), API 1 (160MB), API 2 (160MB), Sistema (20MB).
- **Normalização:** Implementar rigorosamente as fórmulas de *clamp* descritas em `REGRAS_DE_DETECCAO.md` para os 14 índices.
