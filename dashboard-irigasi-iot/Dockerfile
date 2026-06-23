# Gunakan image golang versi 1.24 ke atas sesuai kebutuhan library MQTT
FROM golang:1.24-alpine AS builder

# Set working directory di dalam container
WORKDIR /app

# Salin semua file dari host ke dalam container
COPY . .

# Inisialisasi modul dan unduh dependensi langsung di dalam Docker
RUN go mod init monitoring-iot || true
RUN go get github.com/eclipse/paho.mqtt.golang
RUN go get github.com/gorilla/websocket
RUN go mod tidy

# Build aplikasi menjadi satu binary tunggal yang sangat ringan
RUN CGO_ENABLED=0 GOOS=linux go build -o main .

# Tahap produksi menggunakan image Alpine yang sangat kecil
FROM alpine:latest
WORKDIR /root/

# Salin file binary dan folder templates dari tahap builder
COPY --from=builder /app/main .
COPY --from=builder /app/templates ./templates

EXPOSE 8080

# Jalankan aplikasi
CMD ["./main"]