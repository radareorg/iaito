FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends clang-format-20 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /project
ENTRYPOINT ["clang-format-20"]
