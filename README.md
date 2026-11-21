# StoveMonitor

Use an M5Stack to monitor your multifuel stove's temperature on screen and on a web dashboard, temp logging with graphs, recent temperature trends and audio alerts for too hot or needs more fuel. 
<img width="1011" height="500" alt="image" src="https://github.com/user-attachments/assets/2125215f-c6d8-4dcb-88c3-9e44ad8b097c" /> 
<img width="985" height="381" alt="image" src="https://github.com/user-attachments/assets/c4f7cb2d-2a2b-43f2-99ab-5ed5614d209b" />
<img width="984" height="1006" alt="image" src="https://github.com/user-attachments/assets/36d1717c-ee8c-4132-ad0c-db29959f1720" />

These are my notes ... for my records, you are responsible for your actions, think every step through for yourself ! 

If you are to dim to take responsibility for your own actions and choices please turn of your computer, unplug it and never return to the internet again. 

**Hardware I used**

I based mine on the M5Stack Core2 ESP kit, (£39) 

https://thepihut.com/products/m5stack-core2-v1-3-esp32 

M5Stack DualKmeter Module 13.2 (£22)

https://thepihut.com/products/dual-dualkmeter-module-13-2-max31855 

Wall mount kit (£2) 

https://thepihut.com/products/dual-dualkmeter-module-13-2-max31855 

A left over USB Charger to power the Core2
A left over microSD card to store logs on

Then I needed suitable K Type Thermocuple Sensors, 

I used https://www.amazon.co.uk/dp/B07B7278FF (£6)

Not all K Type Sensors are actually suitably heat rated. 

So all in about £76. 

I have added the larger battery back for £10 (M5Stack Battery Module 13.2 )

**I then built up my specification**

I am looking to use an M5 Stack board, with a battery and two thermistors to monitor my multifuel stoves temperature.
I would like it to use two temperatures from two different thermistor type k probes. One on the Flue and one on the stove top. 
I would also like it to monitor its own internal temperature to make sure it is not getting too hot. (as i am wall mounting it next to a fire)

I would like the screen to come on when the temperature of the stove goes above 40C. 
Then the screen should show the rough time (to check it has not crashed) and the two temperatures, preferably with a clearly visable background colour change depending on the temperatures of the probes. 
Blue for cold, Green for good temperature, Yellow for getting a bit high, Red for to hot. 
These temperature thresholds should also be configureable (So that it can be checked on at a glance). 
I would like the screen to show me it's IP so i don't have to go find it or remember a static. DNS registering something like "stove.local" on the network would be good. 
Indication as to it's power (plugged in and battery capacity left)

I would like audible alerts if the temperature of either probe goes above X (and needs checking on / vents closing), this will need to be chanageable and a sperate far more subtle audio alert if the temperature is going down (and needs more fuel). 
I would like it to log the temperatures (Sensors and internal device) every x seconds and battery capacity, to start with maybe 30 seconds, but i would also like this setting to be configureable. These are to be saved to the Micro SD card.  

I would like a web interface, so that i can check on the temperatures from my desk or my phone. 
When my home network is in range and working, i would like it to connect to the network and obtain an IP by DHCP and if it is not in range, i would like it to show it's own open network so that i can connect to it and check on it from a phone or tablet in a power cut locally or set it up. I would like the home wifi and password to be configurable from within the web interface. 
 
I would like a web interface that shows what the temperatures currently are, with the relevant coloured back grounds so that this can be easily checked on. 
Temperature trend analysis (the temperature as gone up 5c in the last 5 minuets or the temperature has gone down 15c in the last 15 minuets). 
Show some graphs of the last 6 hours of burn time and the last 24 hours.  
The time and date should also be configureable encase it needs to start in a power cut, but would be nice if it could pickup the time from NTP over the network when it is connected to WiFi, i may need to be able to configure its timezone for this to work and specify an NTP server, but could you set a suitable NTP server by default. 

**Then i asked several Ai's to build it**

Learnt some lessons, updated the spec. 

I liked them all, but my current preference is Claud's work ... i have uploaded it here to share. 

**Building unit**

The M5Stack units just slot together, any issues / questions can quickly be resolved by YouTube, asking Google or Gemini 

Attatching the sensors was a bigger challenge ... as the screen shots show, i am still not happy with my several atempts to mount my flue sensor safely. 

Neodynium Magnets degrade at even low temperatures ... so this is down to metal clamps.  

**Installing the software**

Download the files

<img width="939" height="461" alt="image" src="https://github.com/user-attachments/assets/02be1810-c3b5-4b8c-aa35-38ba05439caa" />

Green Button top left and then Download Zip 

Unzip the folders, open StoveMonitor (leave all the files in there and together) 

Download and install Arduino IDE

Open it

1. Install the M5Core2 Board Package : Required for Arduino IDE to recognize the device.

go to File > Preferences (or Settings on Mac), and paste the following URL into the Additional Boards Manager URLs field: https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json

After a while i came to the conclusion it had hung ... so i closed the app and it had actually done the job. 

2. Install the M5Stack Board Definitions:Takes 1-2 minutes.

Go to Tools > Board > Boards Manager, type M5Stack in the search bar, find m5stack by M5Stack Official, and click Install.

Once installed, "M5Stack Arduino" boards will appear in your board list.

3. Select Your Board and Port : Tools menu configuration.

Go to Tools > Board > M5Stack Arduino and select M5Core2. Then, plug your M5Stack Core2 into your computer via a USB-C cable and go to Tools > Port to select the correct COM port (Windows) or /dev/cu.* port (Mac).

The correct port is checked and shows active connection.

4. Install the M5Core2 Library:Required for display, buttons, and touch features.

Go to Tools > Manage Libraries, search for M5Core2, and click Install. When prompted, click Install All to also install dependent libraries like TinyGPS++, M5Unified, etc. 

The library shows as "INSTALLED" in the Library Manager.

We now need to install some more libraries ... open the StoveMonitor.ino file (this should open in Arduino IDE)

Then install the M5Unified library

Click the Library Manager icon on the left sidebar (it looks like a stack of books), or go to Sketch → Include Library → Manage Libraries…
Search for M5Unified.
Click Install on the one by M5Stack.
When it asks about dependencies, choose Install All. This also installs M5GFX, which the display code needs.
While you’re there, search for ArduinoJson (by Benoit Blanchon) and install it as well. Make sure it’s version 7.x. If the IDE offers an older version, pick the latest from the version dropdown.

plug in or remove and replug in your M5Stack board from the USB cable to your computer. Your board should show in the drop down at the top of the page. 

<img width="375" height="137" alt="image" src="https://github.com/user-attachments/assets/95845ec8-7779-4e38-ad4e-37e8b545d5dd" />

Once you have this loaded, Upload the code to your board ... at the top of the screen the right arrow is Upload (this transfers the programe to the board)

<img width="128" height="101" alt="image" src="https://github.com/user-attachments/assets/be06efb1-3c6a-4bfd-b807-573a40b295ce" />

Error messages should be fed into Gemini or other Ai to tell you how to fix. 
