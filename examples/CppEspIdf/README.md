# PsychicHttp Full-Featured C++ ESP-IDF Example

This example demonstrates a complete HTTP server implementation using PsychicHttp with native ESP-IDF APIs (no Arduino dependencies).

## Features

- **Static File Serving**: Serve files from SPIFFS with different content for STA and AP clients
- **WebSocket Support**: Real-time bidirectional communication
- **Server-Sent Events**: EventSource for real-time updates
- **Authentication**: Basic and Digest authentication
- **File Upload**: Both simple and multipart form uploads
- **JSON API**: RESTful API with JSON request/response handling
- **Cookies**: Session management with cookies
- **mDNS**: Automatic discovery via `psychic.local`
- **Redirects**: URL redirection support
- **Custom 404**: Custom error page handling
- **Dual mode**: AP (Access point) and STA (Station) running at the same time

## Missing features with original
Below feature depend on Print class of Arduino for ESP32 and still not porting yet
- TemplatePrinter
- PsychicStreamResponse

## Building and Flashing

1. **Set up your WiFi credentials**:
   Edit `main/secret.h` and add your WiFi credentials:
   ```cpp
   #define WIFI_SSID "your_ssid"
   #define WIFI_PASSWORD "your_password"
   ```

2. **Build the project**:
   ```bash
   idf.py build
   ```

3. **Flash the firmware**:
   ```bash
   idf.py flash
   ```

## Available Endpoints

### Static Files
- `/` - Main page (different content for STA vs AP clients)
- `/img/` - Image files
- `/myfile.txt` - Custom text file
- `/alien.png` - Sample image

### API Endpoints
- `GET /api` - JSON API with query parameters
- `POST /api` - JSON API with POST body
- `GET /ip` - Returns client IP address
- `GET /redirect` - Redirects to `/alien.png`

### Authentication
- `GET /auth-basic` - Basic authentication (admin/admin)
- `GET /auth-digest` - Digest authentication (admin/admin)

### Session Management
- `GET /cookies` - Cookie counter example

### Forms
- `POST /post` - Form parameter handling

### File Upload
- `POST /upload/*` - Simple file upload
- `POST /multipart` - Multipart form upload

### Real-time Communication
- `GET /ws` - WebSocket endpoint
- `GET /events` - Server-Sent Events endpoint

## Testing

1. **Connect to WiFi**: The ESP32 will connect to your WiFi network
2. **Access via mDNS**: Visit `http://psychic.local` in your browser
3. **Direct IP access**: Use the IP address shown in the serial monitor

## WebSocket Testing

Use a WebSocket client to connect to `ws://psychic.local/ws` or `ws://[IP]/ws`. The server will:
- Send "Hello!" on connection
- Echo back any messages sent
- Log connections/disconnections

## EventSource Testing

Visit `http://psychic.local/events` in your browser to see real-time updates every 2 seconds.

## File Upload Testing

1. **Simple upload**: POST a file to `/upload/filename.ext`
2. **Multipart upload**: Use a form with `enctype="multipart/form-data"` and POST to `/multipart`

## Authentication Testing

- **Basic Auth**: Visit `/auth-basic` - browser will prompt for credentials (admin/admin)
- **Digest Auth**: Visit `/auth-digest` - more secure authentication

## Customization

- Modify `setupServerRoutes()` in `main.cpp` to add your own endpoints
- Add files to the `data/` folder to serve additional static content
- Customize authentication credentials in the `app_user` and `app_pass` variables

## Troubleshooting

- **SPIFFS mount fails**: Ensure the partition table includes SPIFFS partition
- **mDNS not working**: Check if your network supports mDNS or use direct IP access
- **Upload fails**: Check SPIFFS space and file permissions
- **WebSocket not connecting**: Ensure your browser supports WebSockets

## Serial Output

The example provides detailed logging via serial console:
- WiFi connection status
- HTTP request/response details
- WebSocket connections
- File upload progress
- Error messages 