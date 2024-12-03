import time
import requests
from PIL import Image
import torch
from torch import nn
from torchvision.models import resnet101
from torchvision import transforms
import io

# Define the ESP32 IP address (make sure this is correct in your network)
esp32_ip = 'http://192.168.233.37/'

# Load the ResNet101 model
device = torch.device("cpu")
model = resnet101(pretrained=False)
model.fc = nn.Linear(model.fc.in_features, 3)
state_dict = torch.load("resnet101_transfer_learning.pth", map_location=device)
model.load_state_dict(state_dict)
model.eval()

# Preprocessing pipeline for the model
transform = transforms.Compose([
    transforms.Resize((224, 224)),
    transforms.ToTensor(),
    transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225])
])

# Class labels (adjust accordingly)
classes = ['BDR', 'DDK', 'TDR']
'''''
#timer and led
timer = 0
led_states = [0, 0, 0, 0]
buzzer_on = False
'''
'''
#to update led states and trigger
def updates_led_and_buzzer():
    global timer, led_states, buzzer_on

    #led update based on timer
    for i in range(4):
        if timer >= (i + 1) * 4:
            led_states[i] = 1 #led on
        else:
            led_states[i] = 0 #led off
    
    #led states to esp32
    led_state_binary = ''.join(map(str, led_states))
    print(f"Updating LEDs: {led_state_binary}")
    requests.post(f"{esp32_ip}leds", data={'states': led_state_binary})

    #buzzer trigger after all leds on & timer reach 16
    if timer >= 16 and not buzzer_on:
        print("Buzzer triggered!")
        buzzer_on = True
        requests.post(f"{esp32_ip}buzzer", data={'state': 'on'})
        time.sleep(3)
        requests.post(f"{esp32_ip}buzzer", data={'state': 'off'})
        buzzer_on = False

#timer & led reset
def reset_timer_and_leds():
    global timer, led_states
    timer = 0
    led_states = [0, 0, 0, 0]

    #turn off all leds
    print("Resetting LEDs and timer")
    requests.post(f"{esp32_ip}leds", data={'states': '0000'})
'''
#coba time loop
try:
    print("Starting real-time classification. Press Ctrl+C to stop.")
    while True:
        # Send a request to capture an image
        response = requests.get(f"{esp32_ip}capture")
        if response.status_code == 200:
            # Open the image from response
            img = Image.open(io.BytesIO(response.content))

            # Process the image with the ResNet101 model
            input_tensor = transform(img).unsqueeze(0).to(device)  # Move tensor to CPU
            with torch.no_grad():
                output = model(input_tensor)  # Pass the tensor through the model
                _, predicted = torch.max(output, 1)  # Get the predicted class

            # Ensure the predicted index is valid
            if 0 <= predicted.item() < len(classes):
                result = classes[predicted.item()]
                print(f"Classification result: {result}")
                ''''
                #results is TIDUR
                if result == "TIDUR":
                    timer += 1
                    updates_led_and_buzzer()
                else:
                    #reset timer
                    reset_timer_and_leds
                '''''
                # Optionally, send classification result back to ESP32
                classification_response = requests.post(
                    f"{esp32_ip}classify", data={'status': result}
                )
                timer_response = requests.get(
                    f"{esp32_ip}timer"
                )
                print(f"Response from ESP32: {classification_response.text}")
            else:
                print(f"Error: Predicted index {predicted.item()} is out of range!")
        else:
            print("Failed to capture image from ESP32")

        # Wait for 2 second before the next classification
        time.sleep(1)

except KeyboardInterrupt:
    print("\nReal-time classification stopped.")