## Frequently asked questions

!!! question ""What kind of connection to use with Loxone system?""
	It depends on expectations: LAN connection is fast (delay in terms of microseconds) with immediate responses, but there is need some workaround in LAN safety settings (subnets, VLANs etc..).  
	RS485 is slower (delay in terms of miliseconds), but is simple, safe and reliable.

!!! question ""How long it will take from pushing the wall switch to actually switching on the lights?""
	Whole procedure of communication from Railduino to Loxone and back can be described as follows:  
	
	1.	Request processing by Railduino (pusching the button) and sending the satet to Loxone – in case of LAN max. 20ms, in case of RS485 max. 30ms  
    2.	Processing in Loxone (reception of information and making of further steps) and sending command to Railduino– in case of LAN max. 25ms, in case of RS485 max. 35ms  
    3.	Processing in Railduino module (switching the lights on) - in case of LAN max. 20ms, in case of RS485 max. 30ms  
	
	So the react time will be max. 95ms (in both cases of LAN or RS485). Delay is minimum, longer delay will be created by powering up the current power source of LED lights.

!!! question ""Is it possible to connect more then one Railduino to the same bus?""
	Yes, every modul has its own address which is set using the DIP switch settings. It is possible to connect up to 15 modules operating at the same bus LAN or RS485.

!!! question ""How is the Railduino programmed with in Loxone Config? Does thy system detect inputs and outputs?""
	It is neccessary to insert the virtual inputs and outputs into the Loxone program and make the correct settings (in case of LAN connection). More details are in the Loxone example file which can be downloaded in the download section.

!!! question ""What is the power sonsumption of Railduino module?""
	In the worst scenario – Railduino module with LAN connection (24V power supply) has consumption of approx. 120mA (with all relays off) and consumption of approx. 230mA (all relays on). Therefore power consumption is approx. 6W max. Minimal consumption can be lower then 2W (no LAN and all relays off).

!!! question ""Digital inputs are not working well, I have to push the switch two times, what is the problem?""
	There is bad settings in the virtual input command settings - Use as digital input must be UNchecked!