#include <ESP_Mail_Client.h>
#include <WiFi.h>
#include <time.h>
#include <vector>

// Pin definitions
#define DOOR 36        // GPIO36 (A0)
#define FLAP 39        // GPIO39 (A1)
#define LED_BUILTIN 2  // Built-in LED on ESP32
#define TRIG_PIN 5     // GPIO5 (D8) - Sensor TRIG
#define ECHO_PIN 13    // GPIO13 (D7) - Sensor ECHO

// WiFi SSID
const char* wifiSSID = " ";			// Type your SSID here
// WiFi Password
const char* wifiPassword = " ";		// Type your passowrd here

// SMTP server configuration data
SMTPSession smtp;
ESP_Mail_Session mailSession;
SMTP_Message message;

const char* smtpServer = "smtp.gmail.com";
const int smtpPort = 465;
const char* emailSender = " ";		// Type your e-mail sender address here
const char* emailSenderPassword = " ";		// Type your e-mail password key here
const char* emailRecipient = " ";	// Type your e-mail recipient address here

// Event cache
struct CachedEvent {
    String time;
    String subject;
    String content;
};
std::vector<CachedEvent> eventCache;

// Time synchronization function
void setupTime() {
	configTime(3600, 0, "pool.ntp.org", "time.nist.gov");		// UTC+1 Timezone (3600 seconds)
	Serial.println("Synchronizing time with NTP server...");
	delay(2000);

	time_t now = time(nullptr);
	while (now < 8 * 3600 * 2) {	// Time must be greater than UNIX epoch (1970)
		delay(1000);
	Serial.print(".");
		now = time(nullptr);
	}
	Serial.println("\nTime synchronized!");
}

// Function to get current time as string
String getFormattedTime() {
	time_t now = time(nullptr);
	struct tm timeinfo;
	localtime_r(&now, &timeinfo);

	char buffer[64];
	strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
	return String(buffer);
}

void setup() {
	Serial.begin(115200);
	
	// Attempting to connect to WiFi
	WiFi.begin(wifiSSID, wifiPassword);
	unsigned long startAttemptTime = millis();
	
	// Wait for WiFi connection (up to 10 seconds)
	while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
		delay(500);
		Serial.print(".");
	}

	// If connection failed, switch to Access Point mode
	if (WiFi.status() != WL_CONNECTED) {
		Serial.println("\nFailed to connect to WiFi. Switching to Access Point mode.");
		setupAccessPoint(); // Function starting Access Point (to be implemented below)
    return;
	}

	Serial.println("\nConnected to WiFi!");
	Serial.print("IP Address: ");
	Serial.println(WiFi.localIP());

	// Time synchronization with NTP
	setupTime();

	// SMTP Configuration
	mailSession.server.host_name = smtpServer;
	mailSession.server.port = smtpPort;
	mailSession.login.email = emailSender;
	mailSession.login.password = emailSenderPassword;
	mailSession.login.user_domain = "";

	// Message Configuration
	message.sender.name = "ESP32";
	message.sender.email = emailSender;
	message.subject = "Test Notification - System is working";
	message.addRecipient("Recipient", emailRecipient);
	message.text.content = "The system is working correctly and has been started.";
	
	// Sending test message
	sendEmail("Test Notification - System is working", "The system is working correctly and has been started.");
	
	// Pin configuration
	pinMode(FLAP, INPUT);
	pinMode(DOOR, INPUT);
	pinMode(LED_BUILTIN, OUTPUT);
	pinMode(TRIG_PIN, OUTPUT);
	pinMode(ECHO_PIN, INPUT);

	digitalWrite(LED_BUILTIN, LOW);		// Turn off LED
}

void setupAccessPoint() {
	// Access Point Configuration
	WiFi.softAP("ESP32_AP", "12345678");
	Serial.println("Access Point started:");
	Serial.println("Network name: ESP32_AP");
	Serial.println("Password: 12345678");
	Serial.print("IP Address: ");
	Serial.println(WiFi.softAPIP());

	// Start server for WiFi configuration
	startWebServer();
}

void startWebServer() {
	// Simple Web Server implementation
	WiFiServer server(80);
	server.begin();
	Serial.println("Web Server started. Waiting for connections...");

	while (true) {
		WiFiClient client = server.available();
		if (client) {
			Serial.println("New connection!");
			String request = client.readStringUntil('\r');
			Serial.println("Request:");
			Serial.println(request);

			// Parsing login data (SSID/Password)
			if (request.indexOf("GET /connect?") >= 0) {
				int ssidIndex = request.indexOf("ssid=") + 5;
				int passIndex = request.indexOf("password=") + 9;

				String newSSID = request.substring(ssidIndex, request.indexOf('&', ssidIndex));
				String newPassword = request.substring(passIndex, request.indexOf(' ', passIndex));
				newSSID.replace("%20", " ");
				newPassword.replace("%20", " ");

				// Saving new WiFi network data
				Serial.println("New WiFi network data:");
				Serial.print("SSID: ");
				Serial.println(newSSID);
				Serial.print("Password: ");
				Serial.println(newPassword);

				// Saving data (you can use SPIFFS or EEPROM to make it persistent)
				WiFi.begin(newSSID.c_str(), newPassword.c_str());
				client.println("HTTP/1.1 200 OK");
				client.println("Content-Type: text/html");
				client.println();
				client.println("<html><body><h1>Connected to new network!</h1></body></html>");
				delay(1000);
				ESP.restart();
			} else {
				// Main login form page
				client.println("HTTP/1.1 200 OK");
				client.println("Content-Type: text/html");
				client.println();
				client.println("<html><body>");
				client.println("<h1>WiFi Configuration</h1>");
				client.println("<form action='/connect'>");
				client.println("SSID: <input type='text' name='ssid'><br>");
				client.println("Password: <input type='password' name='password'><br>");
				client.println("<input type='submit' value='Connect'>");
				client.println("</form>");
				client.println("</body></html>");
			}
			client.stop();
			Serial.println("Connection closed.");
		}
	}
}


void loop() {
	int flapState = digitalRead(FLAP);
	int doorState = digitalRead(DOOR);

	// Event handling
	if (flapState == LOW) {
		String timeNow = getFormattedTime();
		String content = "You have new mail in your mailbox!\nEvent time: " + timeNow;
		handleEvent("New mail in mailbox", content);
	}

	if (doorState == LOW) {
		String timeNow = getFormattedTime();
		String content = "Mail was collected from your mailbox!\nEvent time: " + timeNow;
		handleEvent("Mail has been collected", content);
	}

	delay(2000);		// Delay between measurements
}

// Event handler
void handleEvent(const char* subject, const String& content) {
	if (WiFi.status() == WL_CONNECTED) {
		sendEmail(subject, content);
		sendCachedEvents();
	} else {
		Serial.println("No Internet connection. Saving event to cache.");
		eventCache.push_back({getFormattedTime(), subject, content});
	}
}

// Email sending function
void sendEmail(const char* subject, const String& content) {
	message.subject = subject;
	message.text.content = content.c_str();
	if (!smtp.connect(&mailSession)) {
		Serial.printf("SMTP connection error: %s\n", smtp.errorReason().c_str());
		return;
	}

	if (!MailClient.sendMail(&smtp, &message)) {
		Serial.printf("Error sending message: %s\n", smtp.errorReason().c_str());
	} else {
		Serial.println("Message sent!");
	}

	smtp.closeSession();
}

// Sending cached events
void sendCachedEvents() {
	if (eventCache.empty()) return;

	Serial.println("Sending cached events...");
	for (const auto& event : eventCache) {
		sendEmail(event.subject.c_str(), event.content);
	}
	eventCache.clear();
}

// Distance measurement function
long measureDistance() {
	digitalWrite(TRIG_PIN, LOW);
	delayMicroseconds(2);
	digitalWrite(TRIG_PIN, HIGH);
	delayMicroseconds(10);
	digitalWrite(TRIG_PIN, LOW);

	long duration = pulseIn(ECHO_PIN, HIGH);
	float distance = (duration / 2.0) * 0.0343; 	// Distance in cm

	if (duration == 0 || distance < 2 || distance > 400) {
		return 0;
	}

	return duration;
}

// LED blinking function
void blinkLED(int interval) {
	digitalWrite(LED_BUILTIN, HIGH);
	delay(interval);
	digitalWrite(LED_BUILTIN, LOW);
	delay(interval);
}