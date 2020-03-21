String translate_mqtt_status(int status) {
    switch (status) {
        case MQTT_CONNECTION_TIMEOUT:
            return String("Connection timeout");
        case MQTT_CONNECTION_LOST:
            return String("Connection lost");
        case MQTT_CONNECT_FAILED:
            return String("Connect failed");
        case MQTT_DISCONNECTED:
            return String("Disconnected");
        case MQTT_CONNECTED:
            return String("Connected");
        case MQTT_CONNECT_BAD_PROTOCOL:
            return String("Bad protocol");
        case MQTT_CONNECT_BAD_CLIENT_ID:
            return String("Bad client ID");
        case MQTT_CONNECT_UNAVAILABLE:
            return String("Unavailable");
        case MQTT_CONNECT_BAD_CREDENTIALS:
            return String("Bad credentials");
        case MQTT_CONNECT_UNAUTHORIZED:
            return String("Unauthorized");
        default:
            return String("Unknown error");
    }
}

String translate_wifi_status(int status) {
    switch (status) {
        case WL_NO_SHIELD:
            return String("No WiFi shield installed");
        case WL_IDLE_STATUS:
            return String("Idle");
        case WL_NO_SSID_AVAIL:
            return String("Available");
        case WL_SCAN_COMPLETED:
            return String("Scan completed");
        case WL_CONNECTED:
            return String("Connected");
        case WL_CONNECT_FAILED:
            return String("Connect failed");
        case WL_CONNECTION_LOST:
            return String("Connection lost");
        case WL_DISCONNECTED:
            return String("Disconnected");
        default:
            return String("Unknown error");
    }
}
