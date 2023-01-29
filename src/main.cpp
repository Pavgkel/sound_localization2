#include <Arduino.h>
#include <dsps_fft2r.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <driver/adc.h>
#include "driver/timer.h"

#define POW0 8
#define pin_microphone1 32
#define pin_microphone2 33

const char* ssid="Pavgkel";
const char* password="62b375a80de7";

AsyncWebServer server(80);
AsyncEventSource events("/events");
unsigned long lastTime = 0;  
unsigned long timerDelay = 500;

const signed int DLEN=1<<POW0;
float data0[DLEN*2];
float data1[DLEN*2];
float pr, amp2, tt;
int c;
float angle=0;

float EMA_a_low = 0.3;    
float EMA_a_high = 0.5;
 
float EMA_S_low0[DLEN*2];
float EMA_S_low1[DLEN*2];    
float EMA_S_high0[DLEN*2];
float EMA_S_high1[DLEN*2];
 
float bandpass0[DLEN*2];
float bandpass1[DLEN*2];
int read_raw;

float phase(char cim,char cre)
{
  if(!cre) if(cim<0) return -90.0;
  else return 90.0;
  float d=float(cim)/float(cre);
  float ad=abs(d);
  float f;
  if(ad>=0&&ad<=1) f=ad*45.0;
  else if(ad>1&&ad<4) f=ad*((75.96-45)/(4-1))+36;
  else if(ad>=4&&ad<=128) f=ad*((90-75.96)/(128-4))+75.5;
  if(d<0) return -f;
  return f;
}

void drawFFT(float* data_p,float* im_p,int i)
{
  int amp= sqrt(data_p[i] * data_p[i] + im_p[i] * im_p[i]);
  
  //float f=phase(im_p[i],data_p[i]);
  float f = atan2(im_p[i],data_p[i]);
  f=degrees(f);
  float t= (1/data_p[i]) * (f/360);
  int temperature=20;
  float x=t*(331+0.606*temperature);
  //tt=t;
  //pr=x;
  tt+=t*amp;
  pr+=x*amp;
  amp2+=amp;
}

void drawFFT(float* data_p,float* im_p)
{
  for (int i=1; i<DLEN;i++) drawFFT(data_p,im_p,i);
}

const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
    <script src="https://cdn.jsdelivr.net/npm/p5@1.5.0/lib/p5.js"></script>
    <div class="content">
    <div class="cards">
      <div class="card">
        <p><span class="reading"><span id="loc"> </span> &deg</span></p>
      </div>
    </div>
  </div>
    <script>
    if (!!window.EventSource) {
 var source = new EventSource('/events');
 
 source.addEventListener('open', function(e) {
  console.log("Events Connected");
 }, false);
 source.addEventListener('error', function(e) {
  if (e.target.readyState != EventSource.OPEN) {
    console.log("Events Disconnected");
  }
 }, false);
 
 source.addEventListener('message', function(e) {
  console.log("message", e.data);
 }, false);
 var angle;
 source.addEventListener('Localization', function(e)
 {
  console.log("Localization", e.data);
  angle=e.data;
  document.getElementById("loc").innerHTML = e.data;
 }, false);
}
        function setup() {
            createCanvas(500, 500);
            angleMode(DEGREES);
        }
        
        function draw()
        {
             
            background(51);
            var x = 200;
            var y = 200;
            stroke(255);
            strokeWeight(8);
            point(x,y);
            var r = 100;
            var dx = r * cos(angle);
            var dy = r * sin(angle);
            point(x+dx, y + dy);
            line(x,y,x+dx, y + dy);
        }
        
    </script>
</head>
<body>

</body>
</html>)rawliteral";

String processor(const String& var){
  if(var == "Localization"){
    return String(angle);
  }
  return String();
}

void setup()
{
  Serial.begin(115200);
  //analogReadResolution(12);
  //pinMode(pin_microphone1,INPUT);
  //pinMode(pin_microphone2,INPUT);
  adc1_config_width ( ADC_WIDTH_BIT_9 );
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
 for (int i=0; i < DLEN*2; i++)
  {
    EMA_S_low0[i] =adc1_get_raw(ADC1_CHANNEL_4);
    EMA_S_high0[i] =adc1_get_raw(ADC1_CHANNEL_4);

    adc2_get_raw(ADC2_CHANNEL_9,ADC_WIDTH_9Bit, &read_raw);
    EMA_S_low1[i]=read_raw;
    adc2_get_raw(ADC2_CHANNEL_9,ADC_WIDTH_9Bit, &read_raw);
    EMA_S_high1[i]=read_raw;
  };

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
 
  Serial.println(WiFi.localIP());
  // Handle Web Server
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  // Handle Web Server Events
  events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    // send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client->send("hello!", NULL, millis(), 10000);
  });
  server.addHandler(&events);
  server.begin();
}

void loop()
{
  
    int i;
    int sh=4;
    unsigned long s;
  
    for (i=0; i < (DLEN*2)-1; i++)
    {
      data0[i] =adc1_get_raw(ADC1_CHANNEL_4);
      data0[i+1] =0;
      adc2_get_raw(ADC2_CHANNEL_9,ADC_WIDTH_9Bit, &read_raw);
      data1[i]=read_raw;
      data1[i+1] =0;

      EMA_S_low0[i] = (EMA_a_low*data0[i]) + ((1-EMA_a_low)*EMA_S_low0[i]);
      EMA_S_high0[i] = (EMA_a_high*data0[i]) + ((1-EMA_a_high)*EMA_S_high0[i]);
      EMA_S_low1[i] = (EMA_a_low*data1[i]) + ((1-EMA_a_low)*EMA_S_low1[i]);
      EMA_S_high1[i] = (EMA_a_high*data1[i]) + ((1-EMA_a_high)*EMA_S_high1[i]);
      bandpass0[i] = EMA_S_high0[i] - EMA_S_low0[i];
      bandpass1[i] = EMA_S_high1[i] - EMA_S_low1[i];

    };

    dsps_bit_rev_fc32_ansi(bandpass0,POW0*2);
    dsps_bit_rev_fc32_ansi(bandpass1,POW0*2);

    pr=0;
    amp2=0;
    tt=0;
    drawFFT(bandpass0,bandpass1);
    
    float r0=pr/amp2;
    float r1=tt/amp2;
    float distance=degrees(asin(tt));
    angle = degrees(asin(r0)/0.2);   
    
    
  
    if (angle>0)
    {
      if ((millis() - lastTime) > timerDelay)
    {
      Serial.println(angle);
// Send Events to the Web Server with the Sensor Readings
    events.send("ping",NULL,millis());
    events.send(String(angle).c_str(),"Localization",millis());
    lastTime = millis();
    }
    }
}