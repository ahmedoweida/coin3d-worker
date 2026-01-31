FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /app

# 1) System deps: compiler + Coin3D dev headers/libs + Python runtime for API
RUN apt-get update && apt-get install -y \
  build-essential cmake git curl ca-certificates \
  libcoin-dev \
  python3 python3-pip \
  && rm -rf /var/lib/apt/lists/*

# 2) Python API deps
COPY requirements.txt .
RUN pip3 install --no-cache-dir -r requirements.txt

# 3) Build native converter (produces /app/bin/iv2glb)
COPY native ./native
RUN mkdir -p bin && \
  g++ -O2 -std=c++17 native/iv2glb.cpp -o bin/iv2glb -lCoin

# 4) API server
COPY main.py .
ENV PORT=10000
CMD ["sh", "-c", "uvicorn main:app --host 0.0.0.0 --port ${PORT}"]
