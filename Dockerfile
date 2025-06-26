# ---- Stage 1: Build inside container ----
FROM debian:bookworm AS builder

RUN apt-get update && \
    apt-get install -y build-essential byacc

WORKDIR /src
COPY . .

RUN make all

# ---- Stage 2: Minimal runtime ----
FROM debian:bookworm-slim

# Install useful CLI tools (vi, less, man, bash)
RUN apt-get update && \
    apt-get install -y \
    vim-tiny \
    less \
    man \
    manpages \
    bash \
    gzip && \
    rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/miralib/ /usr/local/lib/miralib/
COPY --from=builder /src/mira.1 /usr/local/share/man/man1/mira.1
COPY --from=builder /src/mira /usr/local/bin/mira

RUN mkdir -p /usr/local/share/man/man1 && \
    chmod +x /usr/local/bin/mira && \
    gzip -f /usr/local/share/man/man1/mira.1 || true

# Create a non-root user and group
RUN groupadd --system mira && useradd --system --create-home --shell /bin/bash --gid mira mira

# Make miralib user-owned
RUN chown -R mira:mira /usr/local/lib/miralib

# Switch to non-root user
USER mira

# Default working directory
WORKDIR /home/mira

ENV MIRALIB=/usr/local/lib/miralib \
    MIRAPROMPT='Miranda> '

ENTRYPOINT ["/usr/local/bin/mira"]
