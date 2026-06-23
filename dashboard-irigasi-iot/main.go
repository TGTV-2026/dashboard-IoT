package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"sync"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/gorilla/websocket"
)

// Struktur data JSON yang diterima dan dikirim dari/ke frontend
type Command struct {
	Topic   string `json:"topic"`
	Payload string `json:"payload"`
}

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true },
}

var clients = make(map[*websocket.Conn]bool)
var broadcast = make(chan []byte)

// Variabel Global
var mqttClient mqtt.Client
var currentMode = "auto" // Default sistem, nantinya akan disinkronkan dari ESP32
var modeMutex sync.Mutex

func main() {
	brokerURL := os.Getenv("MQTT_BROKER_URL")
	if brokerURL == "" {
		brokerURL = "tcp://broker.hivemq.com:1883"
	}

	opts := mqtt.NewClientOptions().AddBroker(brokerURL)
	opts.SetClientID("golang_iot_backend_irigasi") // ID disesuaikan

	mqttClient = mqtt.NewClient(opts)
	if token := mqttClient.Connect(); token.Wait() && token.Error() != nil {
		log.Fatalf("Gagal terhubung ke Broker: %v", token.Error())
	}
	fmt.Println("Berhasil terhubung ke Public Broker HiveMQ!")

	// SUBSCRIBE: Mendengarkan semua topik dari ESP32
	mqttClient.Subscribe("tgtv_ridho/#", 0, messageHandler)

	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		http.ServeFile(w, r, "templates/index.html")
	})
	http.HandleFunc("/ws", handleWebSockets)

	go handleMessages()

	fmt.Println("Server berjalan di http://localhost:8123")
	log.Fatal(http.ListenAndServe(":8123", nil))
}

// Fungsi terpisah untuk menangani pesan MQTT yang masuk
func messageHandler(client mqtt.Client, msg mqtt.Message) {
	topic := msg.Topic()
	payload := string(msg.Payload())

	// Sinkronisasi status mode dari hardware (ESP32) ke Backend
	if topic == "tgtv_ridho/status/mode" {
		modeMutex.Lock()
		if currentMode != payload {
			currentMode = payload
			fmt.Printf("[SYNC] Mode hardware saat ini: %s\n", currentMode)
		}
		modeMutex.Unlock()
	}

	// Teruskan semua pesan dari MQTT ke Frontend via WebSocket
	jsonMsg := fmt.Sprintf(`{"topic":"%s", "payload":"%s"}`, topic, payload)
	broadcast <- []byte(jsonMsg)
}

func handleWebSockets(w http.ResponseWriter, r *http.Request) {
	ws, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("Error upgrade websocket: %v\n", err)
		return
	}
	defer ws.Close()
	clients[ws] = true

	for {
		_, msg, err := ws.ReadMessage()
		if err != nil {
			delete(clients, ws)
			break
		}

		// Parse pesan dari website
		var cmd Command
		if err := json.Unmarshal(msg, &cmd); err == nil {

			// publish jika perintah adalah ubah mode
			if cmd.Topic == "tgtv_ridho/kontrol/mode" {
				// Backend langsung meneruskan perintah ke ESP32.
				// Variabel `currentMode` akan diperbarui secara otomatis
				// saat ESP32 membalas di topik `tgtv_ridho/status/mode`.
				mqttClient.Publish(cmd.Topic, 0, false, cmd.Payload)
				fmt.Printf("=> PUBLISH: Meminta ubah mode ke %s\n", cmd.Payload)
				continue
			}

			// publish perintah kontrol aktuator manual
			modeMutex.Lock()
			mode := currentMode
			modeMutex.Unlock()

			if mode == "manual" {
				mqttClient.Publish(cmd.Topic, 0, false, cmd.Payload)
				fmt.Printf("=> PUBLISH (MANUAL): %s -> %s\n", cmd.Topic, cmd.Payload)
			} else {
				fmt.Printf("=> BLOKIR: Perintah %s -> %s ditolak karena sistem dalam mode AUTO\n", cmd.Topic, cmd.Payload)
			}
		}
	}
}

func handleMessages() {
	for {
		msg := <-broadcast
		for client := range clients {
			err := client.WriteMessage(websocket.TextMessage, msg)
			if err != nil {
				client.Close()
				delete(clients, client)
			}
		}
	}
}
