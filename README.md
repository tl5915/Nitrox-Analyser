# Nitrox-Analyser
Nitrox Analyser (Divesoft DNA clone)

Material required:
- 50mL Syringe
  - cut the tip off and drill to 16mm, trim to approriate length (around 63mm)
  - keep the silicone plug on the plunger, cut holes for USB-C as end plug, drill hole for lanyard
- M16x1 Nut (can be 3D printed: https://www.thingiverse.com/thing:3428400)
- Flow Limiter (can be 3D printed: https://www.printables.com/model/52405-nitrox-analyzer/files)
  - flow_restrictor_1.stl & flow_restrictor_10mm.stl
  - measured the inner diameter of oxygen cell to select appropriate flow_restrictor file
- Oxygen Cell (any 3-Pin Molex type, e.g. Vandagraph R-22VAN)
- Seeed Xiao ESP32-C3
- LM358P Operational Amplifier
- 3mm 20mA Green LED
- 100/3.3k/47k ohm Resistors
- 0.1uF Ceramic Capacitors
- Prototype Board (cut to 7x8)
- 8-Pin Female Headers
- JST-XH (2.54mm) 3-Pin Female Connector 
  - cut the locking arrows off to fit Molex header on oxygen cell


Circuit connection:
- LM358P pin 8 (Vcc) connects to Xiao 3V3 pin
- LM358P pin 4 (Gnd) connects to Xiao Gnd pin and Oxygen Cell pin 1 (-)
- LM358P pin 3 (IN1+) connects to Oxygen Cell pin 3 (+)
- LM358P pin 1 (OUT1) connects to Xiao pin A2
- 3.3k ohm resistor connects between LM358P pin 2 (IN1-) and pin 4 (Gnd)
- 47k ohm resistor connects between LM358P pin 1 (OUT1) and pin 2 (IN2-)
- 0.1uF capacitor connects between LM358P pin 8 and 3
- 0.1uF capacitor connects between Xiao pin A2 and Gnd pin
- 100 ohm resistor connects between Xiao pin D7 and LED long leg (+)
- LED short leg (-) connects to Xiao pin D6
- Connect the antenna for Xiao, antenna can be sticked on the back of circuit board


UI instruction:
- Power with USB-C cable
- Connects to 'Nitrox Analyser' WiFi, password: 12345678
- Browser open URL: 192.168.4.1/upload_page
  - upload 'icon.png' file
- iPhone connects to the WiFi, in Safari open URL: 192.168.4.1
  - bottom middle up arrow button: Add to Home Screen

- Click 'Cal. Low' to calibrate in air
- Click 'Cal. High' to calibrate in oxygen/rich nitrox
- Click '1-Point Calibration' or '2-Point Calibration' to change calibration mode
  - 2-point calibration available if Cal. High is higher than Cal. Low
- Click 'Setting' to enter setting page
  - Click 'Calibrate High %' to change gas used for 'Cal. High': default 99%, can be set lower
  - Click 'PGA Gain' to change gain used for voltage calculation:
    - measure oxygen cell voltage in air with a multimeter and adjust gain to match the readings
    - default 13.8 (highly dependent on resistor variability)
    - only matter if accurate voltage in required, doesn't affect oxygen percentage calculation
  - Click 'Reset Calibration' to reset all calibration values and PGA gain to default
  - Click 'Firmware Update' to update firmware by uploading .bin file
- LED blink number represent the oxygen percentage to the nearest 10%
  - i.e. 21% 2 blinks, 36% 4 blinks, 80% 8 blinks, etc.
  - LED stays off if O2 < 5%, LED stays on if O2 > 95%


![XIAO](https://github.com/user-attachments/assets/92628dc2-1203-4df5-9cec-ce2de9f65083)

![LM358P](https://github.com/user-attachments/assets/b563ac9e-4f2a-46f4-8005-01352d2ef6d4)

![Oxygen Cell Pins](https://github.com/user-attachments/assets/5c718693-d192-4fa3-aa83-9e6fb3a2a578)

![Application](https://github.com/user-attachments/assets/6894ee7f-4e87-4048-bef6-d057e59bdfe7)

![Product](https://github.com/user-attachments/assets/9853c3bc-4ee1-4948-8fd0-8cde303de41d)

![Enclosure](https://github.com/user-attachments/assets/ad1d1af0-f6ba-48ae-a212-db448d5b1964)

![Circuit](https://github.com/user-attachments/assets/d621241e-beb7-4ba8-9ea7-a63a3d2cf5c9)

![Molex](https://github.com/user-attachments/assets/ee2de589-29ee-42ea-957a-d1b495cb2d3e)

![Assembly](https://github.com/user-attachments/assets/f30ab0ac-960e-434c-9a17-6c7a5371962d)

![UI](https://github.com/user-attachments/assets/121be1cc-0cb2-4428-a0f6-33a1d3e32821)
