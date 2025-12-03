import serial

#try:
    #ser = serial.Serial('COM4', 115200)
    #ser.write(b"led_brightness 0x0005 30 60 100")
    #print("Comando inviato con successo!")
    #ser.close()
#except Exception as e:
    #print(f"Errore: {e}")

try:
    ser = serial.Serial('COM4', 115200)

    # Prova con valori a 16 bit
    hue_16bit = 30   # 30° in 16-bit
    saturation_16bit = 60   # 60% in 16-bit
    lightness_16bit = 80   # 100% in 16-bit

    command = f"led_brightness 0x0005 {hue_16bit} {saturation_16bit} {lightness_16bit}"
    print(f"Invio comando: {command}")
    ser.write(command.encode())
    print("Comando inviato con successo!")
    ser.close()
except Exception as e:
    print(f"Errore: {e}")



#import serial
#import time

#ser = serial.Serial('COM4', 115200)

# Ora questi comandi funzioneranno con il TUO firmware!
#commands = ["on", "off", "level 50", "level 100", "status"]

#for cmd in commands:
    #print(f"Inviando: {cmd}")
   # ser.write(f"{cmd}\n".encode())
   # time.sleep(2)

#ser.close()
