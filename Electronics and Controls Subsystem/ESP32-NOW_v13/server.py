from flask import Flask, request, jsonify
import json
import os
from datetime import datetime
import base64 # Required for decoding the image data
import re # Import the regular expression module for robust sanitization

app = Flask(__name__)

# Define log file and image directory paths
LOG_FILE = "sensor_log.txt"
IMAGE_DIR = "images"

# Ensure image directory exists when the server starts
if not os.path.exists(IMAGE_DIR):
    os.makedirs(IMAGE_DIR)
    print(f"INFO (Server Init): Created image directory: {IMAGE_DIR}")

@app.route('/api/sensor', methods=['POST'])
def handle_sensor_data():
    print(f"\n--- {datetime.now().strftime('%Y-%m-%d %H:%M:%S')} ---")
    print("DEBUG (Server): Received POST request to /api/sensor.")
    try:
        data = request.get_json()

        if data is None:
            print("ERROR (Server): request.get_json() returned None. Check 'Content-Type' header or request body format.")
            raw_data_fallback = request.get_data(as_text=True)
            print(f"DEBUG (Server): Raw request data (first 500 chars): {raw_data_fallback[:500]}")
            raise ValueError("No valid JSON data received or JSON parsing failed.")
        
        print(f"DEBUG (Server): Successfully parsed JSON:\n{json.dumps(data, indent=2)}")

        timestamp_from_sensor = data.get('timestamp', 'N/A')
        rfid = data.get('rfid', 'N/A')
        name = data.get('name', 'N/A')
        sex = data.get('sex', 'N/A')
        weight = data.get('weight', 'N/A')
        temperature = data.get('temperature', 'N/A')
        humidity = data.get('humidity', 'N/A')
        
        image_b64 = data.get('image', None)
        image_filename = "No Image Captured"
        if image_b64:
            print(f"DEBUG (Server): Image data found in JSON. Base64 string length: {len(image_b64)} bytes.")
            try:
                image_bytes = base64.b64decode(image_b64)
                print(f"DEBUG (Server): Image decoded from Base64. Binary size: {len(image_bytes)} bytes.")

                # --- MORE ROBUST FILENAME SANITIZATION ---
                # Sanitize timestamp: Replace spaces, colons, and periods with underscores.
                # Remove any other non-alphanumeric characters (except underscores).
                sanitized_timestamp = re.sub(r'[^a-zA-Z0-9_]', '', timestamp_from_sensor.replace(" ", "_").replace(":", "-").replace(".", "_"))
                
                # Sanitize RFID: Ensure it's a string, then remove any characters that are not alphanumeric or underscore.
                # This is the most critical part for `OSError: Invalid argument`
                sanitized_rfid = re.sub(r'[^a-zA-Z0-9_]', '', str(rfid))
                
                # If RFID is empty after sanitization (e.g., if it was originally 'N/A' or invalid), provide a fallback
                if not sanitized_rfid:
                    sanitized_rfid = "unknown_rfid"

                image_filename = f"{sanitized_timestamp}_{sanitized_rfid}.jpeg"
                filepath = os.path.join(IMAGE_DIR, image_filename)

                with open(filepath, "wb") as f:
                    f.write(image_bytes)
                print(f"✅ Image saved successfully: {filepath}")
            except Exception as img_e:
                print(f"ERROR (Server): Failed to process and save image data: {img_e}")
                image_filename = f"ERROR: Image save failed: {str(img_e)}"
        else:
            print("INFO (Server): No 'image' field found in the received JSON payload.")
            
        current_server_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        log_entry = (
            f"[{current_server_time}] (Sensor Time: {timestamp_from_sensor})\n"
            f"  RFID: {rfid}\n"
            f"  Name: {name}\n"
            f"  Sex: {sex}\n"
            f"  Weight: {weight} kg\n"
            f"  Temperature: {temperature} °C\n"
            f"  Humidity: {humidity} %\n"
            f"  Image File: {image_filename}\n"
            f"{'-'*40}\n"
        )
        with open(LOG_FILE, "a") as f:
            f.write(log_entry)
        print(f"INFO (Server): Sensor data for RFID {rfid} logged successfully.")
            
        return jsonify({
            "status": "success",
            "message": "Data received and logged successfully (with image if present)", 
            "received_data": data
        }), 200
        
    except ValueError as e:
        error_msg = f"Client-side data error: {str(e)}"
        print(f"WARNING (Server): {error_msg}")
        raw_data_on_error = request.get_data(as_text=True)
        print(f"DEBUG (Server): Raw data causing error (first 500 chars): {raw_data_on_error[:500]}")
        return jsonify({
            "status": "error",
            "message": error_msg,
            "received_sample": raw_data_on_error[:500]
        }), 400
    except Exception as e:
        error_msg = f"Internal server error: {str(e)}"
        print(f"CRITICAL (Server): {error_msg}")
        raw_data_on_error = request.get_data(as_text=True)
        print(f"DEBUG (Server): Raw data causing error (first 500 chars): {raw_data_on_error[:500]}")
        return jsonify({
            "status": "error",
            "message": error_msg,
            "received_sample": raw_data_on_error[:500]
        }), 500

if __name__ == '__main__':
    print("INFO (Server Init): Starting Flask server...")
    print(f"INFO (Server Init): Log file path: {os.path.abspath(LOG_FILE)}")
    print(f"INFO (Server Init): Image directory path: {os.path.abspath(IMAGE_DIR)}")
    app.run(host='0.0.0.0', port=5000, debug=True)