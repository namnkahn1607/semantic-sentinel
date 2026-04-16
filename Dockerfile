FROM ubuntu:22.04

# Install SSL Credential for HTTP Gateway to call to LLM Provider
RUN apt-get update && apt-get install -y ca-certificates && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Move binaries & scripts into Container
COPY engine/build-release/sentinel_engine .
COPY gateway/build-release/gateway .
COPY start.sh .

# Open port for HTTP Gateway
EXPOSE 8080

# Run startup script
CMD ["./start.sh"]