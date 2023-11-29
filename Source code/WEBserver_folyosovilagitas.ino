#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "Secrets.h" // WIFI_SSID és WIFI_PASS file
const char* PARAM_INPUT_1 = "state";
const int relayPin = 5; // relé a GPIO5 lábra van kötve
const int buttonPin = 4; // riasztó mozgásérzékelő kimenet a GPIO4 lábra kötve
int relay1Status = LOW;          // relé aktuális állapota
int buttonState;             // mozgásérzékelő aktuális állapota
int lastButtonState = LOW;   // mozgásérzékelő uolsó állapota
unsigned long lastDebounceTime = 0;  // az utolsó idő amikor a relay változott
unsigned long debounceDelay = 50;    // várakozási idő, hogy kiszűrjük a zavarokat, ha a mozgásérzékelő túl sűrűn változna
// relay bekapcsolásának időzítése
unsigned long relayStartTime = 0;  // A relay utolsó bekapcsolásának ideje
const unsigned long relayOnTime = 15000; // mozgásérzékeléstől számított bekapcsolva tartási idő (15 másodperc)

AsyncWebServer server(80);// Webszerver létrehozása a 80-as porton

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html> // HTML kód a WEB szerverhez
<head>
  <title>ESP Home Web Server</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    h2 {font-size: 3.0rem;}
    p {font-size: 3.0rem;}
    body {max-width: 600px; margin:0px auto; padding-bottom: 25px;}
    .switch {position: relative; display: inline-block; width: 120px; height: 68px} 
    .switch input {display: none}
    .slider {position: absolute; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; border-radius: 34px}
    .slider:before {position: absolute; content: ""; height: 52px; width: 52px; left: 8px; bottom: 8px; background-color: #fff; -webkit-transition: .4s; transition: .4s; border-radius: 68px}
    input:checked+.slider {background-color: #2196F3}
    input:checked+.slider:before {-webkit-transform: translateX(52px); -ms-transform: translateX(52px); transform: translateX(52px)}
  </style>
</head>
<body>
  <h2>ESP Home Web Server</h2>
  <h3>Folyoso vilagitas</2>
  %relayPlaceholder%
<script>function toggleCheckbox(element) { // Gombnyomásra különböző URL-ekre küld kérést a Relay ki  vagy bekapcsolására
  var xhr = new XMLHttpRequest();
  if(element.checked){ xhr.open("GET", "/update?state=1", true); }
  else { xhr.open("GET", "/update?state=0", true); }
  xhr.send();
}

setInterval(function ( ) { // a WEB server állapotának frissítése/aktualizálása másodpercenként
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var inputChecked;
      var outputStateM;
      if( this.responseText == 1){ 
        inputChecked = true;
        outputStateM = "On";
      }
      else { 
        inputChecked = false;
        outputStateM = "Off";
      }
      document.getElementById("relayPin").checked = inputChecked;
      document.getElementById("outputState").innerHTML = outputStateM;
    }
  };
  xhttp.open("GET", "/state", true);
  xhttp.send();
}, 1000 ) ;
</script>
</body>
</html>
)rawliteral";

String processor(const String& var){ // a placeholder kicsrélése a Weboldalon a kapcsoló aktuális állapotára
  if(var == "relayPlaceholder"){
    String buttons ="";
    String outputStateValue = outputState();
    buttons+= "<h4>Relay - GPIO 5 - State <span id=\"outputState\"></span></h4><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"relayPin\" " + outputStateValue + "><span class=\"slider\"></span></label>";
    return buttons;
  }
  return String();
}

String outputState(){
  if(digitalRead(relayPin)){
    return "checked";
  }
  else {
    return "";
  }
  return "";
}

void setup(){
  Serial.begin(115200); // soros port sebességének beállítása

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);
  pinMode(buttonPin, INPUT);
  
  // kapcsolódás a WIFI-hez
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  delay(1000); // egy kis várakozás, hugy a soros kapcsolat biztos felépüljön az első kiírás előtt
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) { // addig vár amíg nem csatlakozik
    delay(1000);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP address: "); 
  Serial.println(WiFi.localIP()); // kiírja a csatlakozási IP címet

  // ha kérés érkezik valamelyika URL címről elköldjük a Html oldalt
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  // ellenőrizzük, hogy érkezett-e kérés valamelyi URL-ről és annak megfelelően kapcsoljuk a relay-t
  server.on("/update", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
    // GET kérés a WEB kapcsoló értékére
    if (request->hasParam(PARAM_INPUT_1)) {
      inputMessage = request->getParam(PARAM_INPUT_1)->value();
      digitalWrite(relayPin, inputMessage.toInt());
      relay1Status = !relay1Status;
      if (inputMessage.toInt() == HIGH) { //ha a WEB-n bekapcsolom, akkor indítom a bekapcsolva tartási időt
        relayStartTime = millis();
      }
    }
    else {
      inputMessage = "No message sent";
    }
    Serial.println(inputMessage);
    request->send(200, "text/plain", "OK");
  });

  // Ha érkezett kérés a /state URL helyről, akkor elküldjük az aktuális kimeneti állapotot
  server.on("/state", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(digitalRead(relayPin)).c_str());
  });
    server.begin(); // szerver indítása
}
  
void loop() {
  int reading = digitalRead(buttonPin); // mozgásérzékelő állapotának beolvasása
  // ellenőrizzük, hogy a mozgásérzékelő változott-e
  if (reading != lastButtonState) {
    lastDebounceTime = millis(); // aktualizáljuk az utolsó változás idejét
  }
// ha a megadott zavarszűrési időt figyelembevéve változott a mozgásérzékelő állapota
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == HIGH) { //csak akkor állítjuk a relay állapotot ha a mozgásérzékelő állapota HIGH
        relay1Status = !relay1Status;
        digitalWrite(relayPin, HIGH);
        relayStartTime = millis();
      }
    }
      // relay kikapcsolása, ha elértük a bekapcsolva tartási relayOnTime időt
    if ((millis() - relayStartTime) >= relayOnTime) { 
      relay1Status = false; // ez
      digitalWrite(relayPin, LOW);
    }
  }
   // aktualizáljuk az utolsó relay állapotot
  lastButtonState = reading;
}
