#include <SPI.h>
#include <Ethernet.h>
#include <Wire.h>
#include <SD.h>
#include "DS3231.h"
#define switchNo 4
#define FILE_NAME_LEN 20
#define HTTP_invalid 0
#define HTTP_GET 1
#define FT_HTML 0
#define FT_TEXT 2
#define FT_INVALID 3
#define PIN_ETH_SPI 10

const byte mac[] PROGMEM = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
const byte ip[] = {10, 63, 137, 240}; 
const byte gateway[] PROGMEM = {10, 63, 137, 254};
const byte subnet[] PROGMEM = {255, 255, 255, 0};
RTClib RTC;
EthernetServer server(80);

unsigned long debounceDelay = 50;

long counter[switchNo + 1];
bool buttonState[switchNo * 2 + 1];
bool lastButtonState[switchNo * 2 + 1];
bool reading[switchNo * 2 + 1];
unsigned long lastDebounceTime[switchNo * 2 + 1];
int rstPin[] = {0, 29, 31, 33, 35};
int switchPin[] = {0, 28, 30, 32, 34};

int switchRead(int idx) {
  reading[idx] = digitalRead(switchPin[idx]);
  if (reading[idx] != lastButtonState[idx]) {lastDebounceTime[idx] = millis();}
  if ((millis() - lastDebounceTime[idx]) > debounceDelay) {
    if (reading[idx] != buttonState[idx]) {
      buttonState[idx] = reading[idx];
      if (buttonState[idx] == HIGH) {
        return 1;
      }
    }
  }
  lastButtonState[idx] = reading[idx];
  return 0;
}

int rstRead(int idx) {
  idx += switchNo;
  reading[idx] = digitalRead(rstPin[idx - switchNo]);
  if (reading[idx] != lastButtonState[idx]) {lastDebounceTime[idx] = millis();}
  if ((millis() - lastDebounceTime[idx]) > debounceDelay) {
    if (reading[idx] != buttonState[idx]) {
      buttonState[idx] = reading[idx];
      if (buttonState[idx] == LOW) {
        return 1;
      }
    }
  }
  lastButtonState[idx] = reading[idx];
  return 0;
}

void Count() {
  for (int i = 1; i <= switchNo; i++)
    if (switchRead(i)) {
      counter[i]++;
      PrintTime(i);
    }
}

void Reset() {
  for (int i = 1; i <= switchNo; i++)
    if (rstRead(i) == 1) {
      DateTime now = RTC.now();
      String Time = String(now.year(), DEC) + "/" + String(now.month(), DEC) +
                    "/" + String(now.day(), DEC) + " " +
                    String(now.hour(), DEC) + ":" + String(now.minute(), DEC) +
                    ":" + String(now.second(), DEC);
      String dataString = String("Total:") + String(counter[i]) +
                          String(" Reseted at:") + String(" ") + Time;
      String fileName = "fullLog" + String(i) + ".txt";

      File dataFile = SD.open(fileName.c_str(), FILE_WRITE);
      if (dataFile) {
        dataFile.println(dataString);
        dataFile.println("------------------------------------");
        dataFile.close();
      }

      String fileName2 = "log" + String(i) + ".txt";
      String dataString2 = String(counter[i]) + " " + Time;
      File dataFile2 = SD.open(fileName2.c_str(), FILE_WRITE);
      if (dataFile2) {
        dataFile2.println(dataString2);
        dataFile2.close();
      }
      counter[i] = 0;
    }
}

void PrintTime(int idx) {
  DateTime now = RTC.now();
  String Time = String(now.year(), DEC) + "/" + String(now.month(), DEC) + "/" +
                String(now.day(), DEC) + " " + String(now.hour(), DEC) + ":" +
                String(now.minute(), DEC) + ":" + String(now.second(), DEC);
  String dataString = String(counter[idx]) + " " + Time;
  String fileName = "fullLog" + String(idx) + ".txt";

  File dataFile = SD.open(fileName.c_str(), FILE_WRITE);
  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
  }
}

void setup() {
  Wire.begin();
  for (int i = 1; i <= switchNo; i++) {
    pinMode(switchPin[i], INPUT);
    lastButtonState[i] = LOW;
    pinMode(rstPin[i], INPUT);
    lastButtonState[i + switchNo] = LOW;
  }
  pinMode(PIN_ETH_SPI, OUTPUT);
  digitalWrite(PIN_ETH_SPI, HIGH);

  Serial.begin(115200);
  SD.begin(4);
  Ethernet.begin((uint8_t *)mac, ip, gateway, subnet);
  server.begin();
}

void WebIntercomModule() {
  EthernetClient client = server.available();
  if (client) {
    while (client.connected()) {
      if (ServiceClient(&client)) {
        break;
      }
    } 
    delay(1);
    client.stop();
  } 
}

void loop() {
  Count();
  Reset();
  WebIntercomModule();
}

bool ServiceClient(EthernetClient *client) {
  static boolean currentLineIsBlank = true;
  char cl_char;
  File webFile;
  char file_name[FILE_NAME_LEN + 1] = {0};
  char http_req_type = 0;
  char req_file_type = FT_INVALID;
  const char *file_types[] = {"text/html", "text/plain"};

  static char req_line_1[40] = {0};
  static unsigned char req_line_index = 0;
  static bool got_line_1 = false;

  if (client->available()) {
    cl_char = client->read();

    if ((req_line_index < 39) && (got_line_1 == false)) {
      if ((cl_char != '\r') && (cl_char != '\n')) {
        req_line_1[req_line_index] = cl_char;
        req_line_index++;
      } else {
        got_line_1 = true;
        req_line_1[39] = 0;
      }
    }

    if ((cl_char == '\n') && currentLineIsBlank) {
      http_req_type =
          GetRequestedHttpResource(req_line_1, file_name, &req_file_type);
      if (http_req_type == HTTP_GET) {
        if (req_file_type < FT_INVALID) {
          webFile = SD.open(file_name);
          if (webFile) {
            client->println(F("HTTP/1.1 200 OK"));
            client->print(F("Content-Type: "));
            client->println(file_types[req_file_type]);
            client->println(F("Connection: close"));
            client->println();
            while (webFile.available()) {
              int num_bytes_read;
              char byte_buffer[2048];
              num_bytes_read = webFile.read(byte_buffer, 2048);
              client->write(byte_buffer, num_bytes_read);
            }
            webFile.close();
          }
        }
      }
      req_line_1[0] = 0;
      req_line_index = 0;
      got_line_1 = false;
      return 1;
    }
    if (cl_char == '\n') {
      currentLineIsBlank = true;
    } else if (cl_char != '\r') {
      currentLineIsBlank = false;
    }
  }
  return 0;
}

char GetRequestedHttpResource(char *req_line, char *file_name,
                              char *file_type) {
  char request_type = HTTP_invalid;
  char *str_token;

  *file_type = FT_INVALID;

  str_token = strtok(req_line, " ");
  if (strcmp(str_token, "GET") == 0) {
    request_type = HTTP_GET;
    str_token = strtok(NULL, " ");
    if (strcmp(str_token, "/") == 0) {
      strcpy(file_name, "index.htm");
      *file_type = FT_HTML;
    } else if (strlen(str_token) <= FILE_NAME_LEN) {
      strcpy(file_name, str_token);
      str_token = strtok(str_token, ".");
      str_token = strtok(NULL, ".");

      if (strcmp(str_token, "htm") == 0) {
        *file_type = 0;
      } else if (strcmp(str_token, "txt") == 0) {
        *file_type = 2;
      } else {
        *file_type = 3;
      }
    }
  }

  return request_type;
}
