@echo off
set /p Port_var="Na kterem COM portu mate prihlasene Railduino (Arduino Mega 2560) ?(napr. "COM18") "
set /p File_var="Napiste jmeno souboru, ktere chcete zaslat do Railduina (napr. "1_3_UDP_485.hex") "

avrdude -C avrdude.conf -v -p atmega2560 -c wiring -P %Port_var% -b 115200 -D -U flash:w:%File_var%:i

pause