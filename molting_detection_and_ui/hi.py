from flask import Flask, render_template, request, redirect, url_for, flash, jsonify, Response
from werkzeug.utils import secure_filename
import sqlite3
import os
import json
import numpy as np
from datetime import datetime
import torch
from torchvision import transforms, models
from tensorflow.keras.models import load_model
import joblib
import torch.nn as nn
import torch.nn.functional as F
from PIL import Image
import base64
from db import init_db
import threading
import time
import queue
from transformers import OwlViTProcessor, OwlViTForObjectDetection
from flask import make_response
import csv
import io
import logging



# Initialize Flask app
app = Flask(__name__)

app.secret_key = 'supersecretkey'
app.config['UPLOAD_FOLDER'] = 'static/uploads'

# Initialize OwlV2 model for animal detection
owl_processor = OwlViTProcessor.from_pretrained("./owlvit-local")
owl_model = OwlViTForObjectDetection.from_pretrained("./owlvit-local")

# Define animal categories we want to detect
ANIMAL_CATEGORIES = ["penguin", "honey badger", "bird", "seal", "other animal"]
DETECTION_THRESHOLD = 0.25  # Confidence threshold for animal detection

# Create queues and variables to store ESP32 data
esp_data_queue = queue.Queue(maxsize=20)  # Store the last 20 readings
latest_esp_data = None
latest_esp_image = None
esp_clients = []  # List to track connected clients for SSE

# Database and model paths
UPLOAD_FOLDER = 'static/uploads'
ALLOWED_EXTENSIONS = {'png', 'jpg', 'jpeg'}
DB_PATH = 'penguin_molting.db'
MODEL_PATH = r"C:\Users\MKHIN\Downloads\Design Project\penguin_project_code\checkponts\checkpoints\best_model_fold4.pt"
MODEL_VERSION = 'fold4/1'
# Add after your other model loading code
MOLT_STAGE_MODEL_PATH = r"C:\Users\MKHIN\Downloads\Design Project\penguin_project_code\checkponts\checkpoints\molt_stage_model_simplified.h5"  # Update with your actual path
MOLT_STAGE_SCALER_PATH = r"C:\Users\MKHIN\Downloads\Design Project\penguin_project_code\checkponts\checkpoints\molt_stage_scaler.save"  # Scaler for feature normalization

# Load molt stage model and scaler
try:
    molt_stage_model = load_model(MOLT_STAGE_MODEL_PATH)
    molt_stage_scaler = joblib.load(MOLT_STAGE_SCALER_PATH)
    print("Successfully loaded molt stage classifier")
except Exception as e:
    molt_stage_model = None
    molt_stage_scaler = None
    print(f"Error loading molt stage model: {str(e)}")

os.makedirs(UPLOAD_FOLDER, exist_ok=True)
init_db()

# --- Model Loading ---
model = models.vgg16(weights=None)
num_features = model.classifier[6].in_features
model.classifier[6] = nn.Linear(num_features, 2)
state_dict = torch.load(MODEL_PATH, map_location=torch.device('cpu'))
model.load_state_dict(state_dict)
model.eval()

transform = transforms.Compose([
    transforms.Resize((224, 224)),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
])

def allowed_file(filename):
    return '.' in filename and filename.rsplit('.', 1)[1].lower() in ALLOWED_EXTENSIONS

def preprocess_image(filepath):
    img = Image.open(filepath).convert('RGB')
    img = transform(img).unsqueeze(0)
    return img

def predict(filepath):
    img = preprocess_image(filepath)
    with torch.no_grad():
        output = model(img)
        probs = F.softmax(output, dim=1).squeeze()
        molting_prob = probs[0].item()
        normal_prob = probs[1].item()
    return molting_prob, normal_prob
def get_molting_stage(weight, sex, detection_date):
    """
    Determine molt stage for MOLTING penguins only using ML model
    Returns tuple of (stage_name, confidence)
    
    Args:
        weight: float - penguin weight in kg
        sex: str - 'Male' or 'Female'
        detection_date: datetime - date of observation
    
    Returns:
        tuple: (stage_name: str, confidence: float)
    """
    # Validate ML components
    if molt_stage_model is None or molt_stage_scaler is None:
        raise ValueError("Molt stage classifier not properly initialized")
    
    try:
        # Convert sex to numerical (0=female, 1=male)
        sex_code = 0 if sex and sex.lower() == 'female' else 1
        
        # Extract temporal features
        day_of_year = detection_date.timetuple().tm_yday
        
        # Create cyclical features for seasonality
        day_sin = np.sin(day_of_year * (2 * np.pi / 365))
        day_cos = np.cos(day_of_year * (2 * np.pi / 365))
        
        # Prepare features array (order must match training)
        features = np.array([[weight, sex_code, day_of_year, day_sin, day_cos]])
        
        # Normalize features
        scaled_features = molt_stage_scaler.transform(features)
        
        # Get prediction
        predictions = molt_stage_model.predict(scaled_features)
        stage_idx = np.argmax(predictions)
        confidence = np.max(predictions)
        
        # Map index to stage name
        stage_mapping = {
            0: "Pre-molt",
            1: "Mid-molt", 
            2: "Post-molt"
        }
        
        return stage_mapping[stage_idx], float(confidence)
        
    except Exception as e:
        print(f"Error in ML molt stage prediction: {str(e)}")
        raise RuntimeError("Failed to predict molt stage using ML model")

def get_previous_weight(penguin_id):
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    result = cursor.execute(
        "SELECT last_weight FROM penguins WHERE rfid = ?",
        (penguin_id,)
    ).fetchone()
    conn.close()
    return result[0] if result else None

def determine_health_status(weight_kg, molting_prob, daily_change=None):
    if weight_kg < 3.0:
        return "Underweight"
    elif daily_change and daily_change < -0.2:
        return "Rapid Weight Loss"
    elif molting_prob > 0.5:
        return "Molting"
    else:
        return "Healthy"

def detect_animal(image_path):
    """Use OwlV2 to detect if the image contains a penguin or other animal"""
    image = Image.open(image_path).convert('RGB')
    
    # Prepare inputs and run detection
    inputs = owl_processor(text=ANIMAL_CATEGORIES, images=image, return_tensors="pt")
    outputs = owl_model(**inputs)
    
    # Target image sizes (height, width) to rescale box predictions
    target_sizes = torch.Tensor([image.size[::-1]])
    
    # Convert outputs (bounding boxes and class logits) to COCO API
    results = owl_processor.post_process_object_detection(
        outputs=outputs, 
        target_sizes=target_sizes,
        threshold=DETECTION_THRESHOLD
    )
    
    # Process results
    is_penguin = False
    animal_info = []
    
    if len(results) > 0 and 'scores' in results[0] and len(results[0]['scores']) > 0:
        for score, label in zip(results[0]['scores'], results[0]['labels']):
            animal_type = ANIMAL_CATEGORIES[label.item()]
            confidence = score.item()
            
            if animal_type == "penguin" and confidence >= DETECTION_THRESHOLD:
                is_penguin = True
            
            animal_info.append(f"{animal_type} (confidence: {confidence:.2f})")
    
    notes = "Detected animals: " + ", ".join(animal_info) if animal_info else "No animals detected"
    return is_penguin, notes

import os
import base64
import sqlite3
from datetime import datetime
from PIL import Image
from werkzeug.utils import secure_filename

def process_detection(rfid, image_file_or_b64, weight, sex=None, env_data=None):
    """Process penguin detection with ML-based molt stage classification."""

    # Ensure upload folder exists
    os.makedirs(app.config['UPLOAD_FOLDER'], exist_ok=True)

    now = datetime.now()
    detection_time_str = now.strftime('%Y-%m-%d %H:%M:%S')

    # Save image (base64 string or Werkzeug file)
    if isinstance(image_file_or_b64, str):
        # Handle possible data URI prefix
        if image_file_or_b64.startswith('data:image'):
            _, encoded = image_file_or_b64.split(',', 1)
        else:
            encoded = image_file_or_b64

        try:
            file_bytes = base64.b64decode(encoded)
        except Exception as e:
            raise RuntimeError(f"Base64 decode error: {e}")

        filename = secure_filename(f"{rfid}_{now.strftime('%Y%m%d_%H%M%S')}.jpg")
        filepath = os.path.join(app.config['UPLOAD_FOLDER'], filename)

        try:
            with open(filepath, "wb") as f_out:
                f_out.write(file_bytes)
        except Exception as e:
            raise RuntimeError(f"Failed to save image: {e}")

    else:
        # Werkzeug file object
        ext = image_file_or_b64.filename.rsplit('.', 1)[1].lower()
        filename = secure_filename(f"{rfid}_{now.strftime('%Y%m%d_%H%M%S')}.{ext}")
        filepath = os.path.join(app.config['UPLOAD_FOLDER'], filename)
        image_file_or_b64.save(filepath)

    # Validate that the image can be opened properly
    try:
        img = Image.open(filepath).convert('RGB')
    except Exception as e:
        os.remove(filepath)
        raise RuntimeError(f"Invalid image file saved: {e}")

    image_url = f"/static/uploads/{filename}"

    # Detect animal type and notes
    is_penguin, animal_notes = detect_animal(filepath)

    # Initialize defaults for molt detection
    molting_prob = 0.0
    normal_prob = 0.0
    molting_prediction = 0
    confidence = 0.0
    stage_name = "Unknown"
    health = "Unknown"
    notes = animal_notes
    status_color = "black"
    daily_change = 0.0

    if is_penguin:
        molting_prob, normal_prob = predict(filepath)
        molting_prediction = int(molting_prob > normal_prob)
        confidence = float(max(molting_prob, normal_prob))

        prev_weight = get_previous_weight(rfid)
        daily_change = round(float(weight) - prev_weight, 2) if prev_weight else 0.0

        if molting_prob >= 0.5:
            try:
                stage_name, stage_confidence = get_molting_stage(
                    weight=float(weight),
                    sex=sex,
                    detection_date=now
                )
                health = "Molting"
                notes = f"{animal_notes} | ML Stage Confidence: {stage_confidence:.2f}"
            except Exception as e:
                print(f"ML stage prediction failed: {str(e)}")
                if molting_prob < 0.7:
                    stage_name = "Early-molt"
                else:
                    stage_name = "Late-molt"
                health = "Molting"
                notes = f"{animal_notes} | Fallback staging used"
        else:
            stage_name = "Non-molting"
            health = determine_health_status(float(weight), molting_prob, daily_change)

        if health == "Molting":
            status_color = "orange"
        elif health in ["Underweight", "Rapid Weight Loss"]:
            status_color = "red"
        else:
            status_color = "green"

    else:
        stage_name = "Not a Penguin"
        health = "Danger"
        status_color = "red"
        notes = animal_notes

    # Database operations
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()

    cursor.execute(
        '''INSERT INTO detections (
            rfid, image_path, detection_time, molting_prediction, confidence, 
            model_version, processed, weight_kg, stage_name, daily_change, health)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)''',
        (rfid, image_url, detection_time_str, molting_prediction, confidence,
         "ESP CAM" if isinstance(image_file_or_b64, str) else "Manual",
         True, weight, stage_name, daily_change, health)
    )

    penguin = cursor.execute('SELECT * FROM penguins WHERE rfid = ?', (rfid,)).fetchone()
    if penguin:
        cursor.execute('''
            UPDATE penguins
            SET last_detection_time=?, current_molting_status=?, molting_confidence=?,
                last_weight=?, sex=COALESCE(?, sex), stage_name=?, daily_change=?, health=?,
                notes=?
            WHERE rfid=?
        ''', (detection_time_str, molting_prediction, confidence, weight, sex,
              stage_name, daily_change, health, notes, rfid))
    else:
        cursor.execute('''
            INSERT INTO penguins (
                rfid, last_weight, current_molting_status, molting_confidence,
                last_detection_time, first_seen, sex, stage_name, daily_change, health,
                notes)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ''', (rfid, weight, molting_prediction, confidence, detection_time_str,
              detection_time_str, sex, stage_name, daily_change, health, notes))

    if env_data:
        cursor.execute('''
            INSERT INTO environmental_data (
                date, temperature, humidity, light_level, pressure)
            VALUES (?, ?, ?, ?, ?)
        ''', (detection_time_str, env_data.get('temperature', 0),
              env_data.get('humidity', 0), env_data.get('light_level', 0),
              env_data.get('pressure', 0)))

    conn.commit()
    conn.close()

    return {
        'rfid': rfid,
        'image_url': image_url,
        'detection_time': detection_time_str,
        'is_penguin': is_penguin,
        'animal_notes': animal_notes,
        'molting_prediction': bool(molting_prediction),
        'confidence': confidence,
        'weight': weight,
        'sex': sex,
        'model_version': "ESP CAM" if isinstance(image_file_or_b64, str) else "Manual",
        'stage_name': stage_name,
        'daily_change': daily_change,
        'health': health,
        'status_color': status_color,
        'notes': notes
    }

#  ESP32 Endpoints
@app.route('/api/esp32-live', methods=['POST'])
def esp32_live():
    global latest_esp_data, latest_esp_image
    try:
        if request.is_json:
            data = request.get_json()
            if 'timestamp' not in data:
                data['timestamp'] = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
                
            if 'image' in data:
                image_b64 = data['image']
                latest_esp_image = image_b64
                try:
                    file_bytes = base64.b64decode(image_b64)
                    filename = f"esp_live_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jpg"
                    filepath = os.path.join(app.config['UPLOAD_FOLDER'], filename)
                    with open(filepath, "wb") as f_out:
                        f_out.write(file_bytes)
                    data['image_path'] = f"/static/uploads/{filename}"
                except Exception as img_err:
                    print(f"Error saving image: {str(img_err)}")
                    data['image_path'] = None
                del data['image']
                
            latest_esp_data = data
            if esp_data_queue.full():
                esp_data_queue.get()
            esp_data_queue.put(data)
            
            if data.get('log_to_db', False):
                conn = sqlite3.connect(DB_PATH)
                cursor = conn.cursor()
                cursor.execute(
                    '''INSERT INTO environmental_data (date, temperature, humidity, light_level, pressure)
                       VALUES (?, ?, ?, ?, ?)''',
                    (data['timestamp'], 
                     data.get('temperature', 0),
                     data.get('humidity', 0),
                     data.get('light_level', 0),
                     data.get('pressure', 0))
                )
                conn.commit()
                conn.close()
                
            return jsonify({"success": True, "message": "Live data received"})
            
        elif request.files and 'image' in request.files:
            file = request.files['image']
            form_data = request.form.to_dict()
            
            if file and allowed_file(file.filename):
                filename = secure_filename(f"esp_live_{datetime.now().strftime('%Y%m%d_%H%M%S')}.{file.filename.rsplit('.', 1)[1].lower()}")
                filepath = os.path.join(app.config['UPLOAD_FOLDER'], filename)
                file.save(filepath)
                
                data = form_data
                data['timestamp'] = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
                data['image_path'] = f"/static/uploads/{filename}"
                latest_esp_data = data
                
                with open(filepath, 'rb') as img_file:
                    img_data = img_file.read()
                    latest_esp_image = base64.b64encode(img_data).decode('utf-8')
                
                if esp_data_queue.full():
                    esp_data_queue.get()
                esp_data_queue.put(data)
                
                return jsonify({"success": True, "message": "Live data with image received"})
            
            return jsonify({"error": "Invalid image file"}), 400
            
        else:
            return jsonify({"error": "No data or image provided"}), 400
        
    except Exception as e:
        print(f"Error in ESP32 live data endpoint: {str(e)}")
        return jsonify({"error": str(e)}), 500

@app.route('/api/esp32-live-data', methods=['GET'])
def get_esp32_live_data():
    try:
        if latest_esp_data:
            response_data = {
                "success": True,
                "data": latest_esp_data,
                "has_image": latest_esp_image is not None
            }
            if 'image_path' in latest_esp_data:
                response_data['image_path'] = latest_esp_data['image_path']
            return jsonify(response_data)
        else:
            return jsonify({"success": False, "message": "No data available yet"}), 404
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/api/esp32-live-image', methods=['GET'])
def get_esp32_live_image():
    try:
        if latest_esp_image:
            return jsonify({"success": True, "image": latest_esp_image})
        else:
            return jsonify({"success": False, "message": "No image available yet"}), 404
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/api/esp32-live-history', methods=['GET'])
def get_esp32_live_history():
    try:
        data_list = list(esp_data_queue.queue)
        return jsonify({"success": True, "data": data_list})
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/api/esp32-sse')
def esp32_sse():
    def event_stream():
        client_queue = queue.Queue()
        esp_clients.append(client_queue)
        try:
            if latest_esp_data:
                yield f"data: {json.dumps(latest_esp_data)}\n\n"
            while True:
                try:
                    data = client_queue.get(timeout=30)
                    yield f"data: {json.dumps(data)}\n\n"
                except queue.Empty:
                    yield ": ping\n\n"
        finally:
            esp_clients.remove(client_queue)

    return Response(event_stream(), 
                   mimetype="text/event-stream", 
                   headers={"Cache-Control": "no-cache", 
                            "X-Accel-Buffering": "no"})

def broadcast_esp_data():
    while True:
        if latest_esp_data and esp_clients:
            for client_queue in esp_clients:
                if not client_queue.full():
                    client_queue.put(latest_esp_data)
        time.sleep(0.5)

broadcast_thread = threading.Thread(target=broadcast_esp_data, daemon=True)
broadcast_thread.start()

# Main application routes
@app.route('/detection.html', methods=['GET', 'POST'])
def detection():
    if request.method == 'POST':
        rfid = request.form.get('rfid')
        file = request.files.get('image')
        weight = request.form.get('weight')
        sex = request.form.get('sex', None)
        
        if not rfid or not weight or not file or not allowed_file(file.filename):
            if request.headers.get('X-Requested-With'):
                return jsonify({'error': 'Missing required fields'}), 400
            flash("Missing required fields or invalid file type.", 'danger')
            return redirect(request.url)
            
        weight = float(weight)
        result = process_detection(rfid, file, weight, sex)

        # Always return JSON for API requests
        if request.headers.get('X-Requested-With'):
            return jsonify({'success': True, **result})
            
        # Only return HTML/redirect for normal form submissions
        flash(f"Detection saved for RFID {rfid}", 'success')
        return redirect(url_for('detection'))
        
    return render_template('detection.html')
@app.route('/api/esp32-detection', methods=['POST'])
def esp32_detection():
    try:
        detection_time = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        if not request.is_json:
            return jsonify({'error': 'Content-Type must be application/json'}), 400
        
        data = request.get_json()
        
        # Validate required fields
        required_fields = ['rfid', 'weight', 'image_base64']
        for field in required_fields:
            if field not in data:
                return jsonify({'error': f'Missing required field: {field}'}), 400
        
        try:
            weight = float(data['weight'])
            temperature = float(data.get('temperature', 0))
            humidity = float(data.get('humidity', 0))
            light_level = int(data.get('light', 0))
            pressure = int(data.get('pressure', 0))
        except (ValueError, TypeError):
            return jsonify({'error': 'Invalid numeric value'}), 400

        # Pass the base64 string directly, do NOT decode here
        image_base64 = data['image_base64']

        result = process_detection(
            rfid=data['rfid'],
            image_file_or_b64=image_base64,
            weight=weight,
            sex=data.get('sex'),
            env_data={
                'temperature': temperature,
                'humidity': humidity,
                'light_level': light_level,
                'pressure': pressure
            }
        )
        
        # Save/update latest detection data globally (optional)
        global latest_esp_data
        latest_esp_data = {
            'rfid': data['rfid'],
            'weight': weight,
            'temperature': temperature,
            'humidity': humidity,
            'light_level': light_level,
            'pressure': pressure,
            'timestamp': detection_time,
            'image_path': result.get('image_url', ''),
            'health': result.get('health', 'Danger'),
            'stage_name': result.get('stage_name', '--'),
            'confidence': result.get('confidence', '--'),
            'is_penguin': result.get('is_penguin', False)
        }

        return jsonify({
            'success': True,
            'message': 'Detection processed successfully',
            'detection_time': detection_time,
            **result
        })

    except Exception as e:
        logging.error(f"Unexpected error in ESP32 detection: {str(e)}", exc_info=True)
        return jsonify({'success': False, 'error': 'Internal server error'}), 500

@app.route('/')
def home():
    return redirect(url_for('index'))

@app.route('/index.html')
def index():
    return render_template('index.html')

@app.route('/manual.html')
def manual():
    return render_template('manual.html')

@app.route('/database.html')
def database():
    return render_template('database.html')

@app.route('/history.html')
def history():
    return render_template('history.html')

# API endpoints
@app.route('/api/penguin/<string:penguin_id>')
def api_penguin_detail(penguin_id):
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    penguin = conn.execute('SELECT * FROM penguins WHERE rfid = ?', (penguin_id,)).fetchone()
    detections = conn.execute(
        '''SELECT d.*, e.temperature, e.humidity, e.light_level, e.pressure 
           FROM detections d
           LEFT JOIN environmental_data e ON d.detection_time = e.date
           WHERE d.rfid = ? 
           ORDER BY d.detection_time DESC''', 
        (penguin_id,)
    ).fetchall()
    conn.close()

    if penguin is None:
        return jsonify({'success': False, 'message': 'Penguin not found'}), 404

    return jsonify({
        'success': True,
        'penguin': dict(penguin),
        'detections': [dict(d) for d in detections]
    })

@app.route('/penguin/<string:penguin_id>')
def penguin_detail_page(penguin_id):
    return render_template('penguin_detail.html', penguin_id=penguin_id)

@app.route('/api/dashboard-stats')
def dashboard_stats():
    conn = sqlite3.connect(DB_PATH)
    total_penguins = conn.execute('SELECT COUNT(*) FROM penguins').fetchone()[0]
    
    has_health_column = False
    columns = conn.execute("PRAGMA table_info(penguins)").fetchall()
    column_names = [col[1] for col in columns]
    has_health_column = 'health' in column_names

    if has_health_column:
        healthy_today = conn.execute('''
            SELECT COUNT(*) FROM penguins 
            WHERE health = 'Healthy'
            AND date(last_detection_time) = date('now')
        ''').fetchone()[0]
        molting = conn.execute('SELECT COUNT(*) FROM penguins WHERE health = "Molting"').fetchone()[0]
        needs_attention = conn.execute('''
            SELECT COUNT(*) FROM penguins 
            WHERE health IN ('Underweight', 'Rapid Weight Loss')
        ''').fetchone()[0]
        danger = conn.execute('''
            SELECT COUNT(*) FROM penguins 
            WHERE health = 'Not a Danger'
        ''').fetchone()[0]
    else:
        healthy_today = conn.execute('''
            SELECT COUNT(*) FROM penguins 
            WHERE current_molting_status = 0 
            AND date(last_detection_time) = date('now')
        ''').fetchone()[0]
        molting = conn.execute('SELECT COUNT(*) FROM penguins WHERE current_molting_status = 1').fetchone()[0]
        needs_attention = conn.execute('''
            SELECT COUNT(*) FROM penguins 
            WHERE (last_weight < 3.5) OR (current_molting_status = 1 AND molting_confidence > 0.7)
        ''').fetchone()[0]
        danger = 0  # Can't determine without health column

    has_detection_columns = False
    detection_columns = conn.execute("PRAGMA table_info(detections)").fetchall()
    detection_column_names = [col[1] for col in detection_columns]
    has_detection_columns = all(col in detection_column_names for col in ['weight_kg', 'stage_name', 'daily_change', 'health'])

    if has_detection_columns:
        recent_detections = conn.execute('''
            SELECT d.rfid, d.detection_time, d.molting_prediction, d.weight_kg, d.stage_name, d.daily_change, d.health
            FROM detections d
            ORDER BY d.detection_time DESC
            LIMIT 5
        ''').fetchall()
    else:
        recent_detections = conn.execute('''
            SELECT d.rfid, d.detection_time, d.molting_prediction, p.last_weight
            FROM detections d
            JOIN penguins p ON d.rfid = p.rfid
            ORDER BY d.detection_time DESC
            LIMIT 5
        ''').fetchall()

    env_data = None
    tables = conn.execute("SELECT name FROM sqlite_master WHERE type='table'").fetchall()
    table_names = [table[0] for table in tables]

    if 'environmental_data' in table_names:
        env_data = conn.execute('''
            SELECT * FROM environmental_data
            ORDER BY date DESC
            LIMIT 1
        ''').fetchone()

    conn.close()

    response = {
        'total_penguins': total_penguins,
        'healthy_today': healthy_today,
        'molting': molting,
        'needs_attention': needs_attention,
        'danger': danger,
        'recent_detections': [
            {
                'rfid': row[0],
                'detection_time': row[1],
                'molting_prediction': bool(row[2]),
                'weight_kg': row[3],
                'stage_name': row[4] if len(row) > 4 else 'Unknown',
                'daily_change': row[5] if len(row) > 5 else 0,
                'health': row[6] if len(row) > 6 else 'Unknown'
            } for row in recent_detections
        ]
    }

    if env_data:
        response['latest_env_data'] = {
            'temperature': env_data[1] if env_data else None,
            'humidity': env_data[2] if env_data else None,
            'light_level': env_data[3] if env_data else None,
            'pressure': env_data[4] if env_data else None
        }

    return jsonify(response)
@app.route('/api/recent-detections')
def recent_detections():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    
    # Query to get recent detections with status color calculation
    detections = conn.execute('''
        SELECT 
            d.*, 
            p.sex,
            p.notes as animal_notes,
            d.model_version as detection_type,  
            CASE 
                WHEN d.health IN ('Underweight', 'Rapid Weight Loss') THEN 'red'
                WHEN d.health = 'Molting' THEN 'orange'
                WHEN d.health = 'Healthy' THEN 'green'
                WHEN d.health = 'Danger' THEN 'red'
                ELSE 'black'
            END as status_color,
            CASE
                WHEN d.stage_name = 'Not a Penguin' THEN 0
                ELSE 1
            END as is_penguin
        FROM detections d
        LEFT JOIN penguins p ON d.rfid = p.rfid
        ORDER BY d.detection_time DESC
        LIMIT 20
    ''').fetchall()
    
    conn.close()

    return jsonify([
        {
            'id': row['id'],
            'rfid': row['rfid'],
            'image_path': row['image_path'],
            'detection_time': row['detection_time'],
            'molting_prediction': bool(row['molting_prediction']),
            'confidence': row['confidence'],
            'model_version': row['model_version'],
            'processed': bool(row['processed']),
            'weight_kg': row['weight_kg'],
            'stage_name': row['stage_name'],
            'daily_change': row['daily_change'],
            'health': row['health'],
            'sex': row['sex'],
            'status_color': row['status_color'],
            'detection_type': row['detection_type'],
            'is_penguin': bool(row['is_penguin']),
            'animal_notes': row['animal_notes'] if 'animal_notes' in row.keys() else ''
        } 
        for row in detections
    ])

@app.route('/api/penguins')
def api_penguins():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    penguins = conn.execute('''
    SELECT p.*,
    (SELECT COUNT(*) FROM detections d WHERE d.rfid = p.rfid) as detection_count
    FROM penguins p
    ORDER BY p.last_detection_time DESC
    ''').fetchall()
    conn.close()

    return jsonify([
        {
            'rfid': row['rfid'],
            'weight_kg': row['last_weight'],
            'current_molting_status': bool(row['current_molting_status']),
            'molting_confidence': row['molting_confidence'],
            'last_detection_time': row['last_detection_time'],
            'first_seen': row['first_seen'],
            'notes': row['notes'] if 'notes' in row.keys() else '',
            'sex': row['sex'] if 'sex' in row.keys() else 'Unknown',
            'stage_name': row['stage_name'] if 'stage_name' in row.keys() else '',
            'daily_change': row['daily_change'] if 'daily_change' in row.keys() else 0,
            'health': row['health'] if 'health' in row.keys() else '',
            'detection_count': row['detection_count'] if 'detection_count' in row.keys() else 0
        } for row in penguins
    ])

@app.route('/api/update-penguin', methods=['POST'])
def update_penguin():
    try:
        rfid = request.form.get('rfid')
        weight = float(request.form.get('weight', 0))
        sex = request.form.get('sex', 'unknown')
        notes = request.form.get('notes', '')

        if not rfid:
            return jsonify({'success': False, 'error': 'RFID is required'}), 400
            
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        penguin = cursor.execute('SELECT * FROM penguins WHERE rfid = ?', (rfid,)).fetchone()
        
        if penguin:
            cursor.execute('''
                UPDATE penguins
                SET last_weight = ?,
                    sex = ?,
                    notes = ?
                WHERE rfid = ?
            ''', (weight, sex, notes, rfid))
        else:
            current_time = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
            cursor.execute('''
                INSERT INTO penguins (rfid, last_weight, current_molting_status, molting_confidence, 
                                     last_detection_time, first_seen, notes, sex, stage_name, daily_change, health)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ''', (rfid, weight, 0, 0, current_time, current_time, notes, sex, 'Non-molting', 0, 'Healthy'))
        
        conn.commit()
        conn.close()
        
        return jsonify({
            'success': True,
            'message': f'Penguin {rfid} information updated successfully'
        })
        
    except Exception as e:
        return jsonify({
            'success': False,
            'error': str(e)
        }), 500

@app.route('/api/environmental-data')
def environmental_data():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    env_data = conn.execute('''
    SELECT * FROM environmental_data
    ORDER BY date DESC
    LIMIT 50
    ''').fetchall()
    conn.close()

    return jsonify([dict(row) for row in env_data])

@app.route('/api/export-detections', methods=['GET'])
def export_detections():
    try:
        file_format = request.args.get('format', 'csv').lower()
        penguin_id = request.args.get('penguin_id', None)
        
        conn = sqlite3.connect(DB_PATH)
        conn.row_factory = sqlite3.Row
        
        query = '''
            SELECT d.*, p.sex, p.notes as penguin_notes 
            FROM detections d
            LEFT JOIN penguins p ON d.rfid = p.rfid
        '''
        params = []
        
        if penguin_id:
            query += ' WHERE d.rfid = ?'
            params.append(penguin_id)
            
        query += ' ORDER BY d.detection_time DESC'
        
        detections = conn.execute(query, params).fetchall()
        conn.close()
        
        if not detections:
            return jsonify({'success': False, 'error': 'No detections found'}), 404
        
        fieldnames = [
            'id', 'rfid', 'detection_time', 'image_path', 'molting_prediction',
            'confidence', 'weight_kg', 'stage_name', 'daily_change', 'health',
            'sex', 'penguin_notes'
        ]
        
        if file_format == 'csv':
            output = io.StringIO()
            writer = csv.DictWriter(output, fieldnames=fieldnames)
            writer.writeheader()
            
            for detection in detections:
                row = {field: detection[field] for field in fieldnames}
                row['molting_prediction'] = 'Yes' if row['molting_prediction'] else 'No'
                writer.writerow(row)
            
            response = make_response(output.getvalue())
            response.headers['Content-Disposition'] = f'attachment; filename=detections_{penguin_id or "all"}.csv'
            response.headers['Content-Type'] = 'text/csv'
            return response
            
        elif file_format == 'txt':
            output = io.StringIO()
            output.write('\t'.join(fieldnames) + '\n')
            
            for detection in detections:
                row = [str(detection[field]) for field in fieldnames]
                row[fieldnames.index('molting_prediction')] = 'Yes' if detection['molting_prediction'] else 'No'
                output.write('\t'.join(row) + '\n')
            
            response = make_response(output.getvalue())
            response.headers['Content-Disposition'] = f'attachment; filename=detections_{penguin_id or "all"}.txt'
            response.headers['Content-Type'] = 'text/plain'
            return response
            
        return jsonify({'success': False, 'error': 'Invalid format specified'}), 400
        
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)}), 500
@app.route('/upload', methods=['POST'])
def handle_upload():
    try:
        # Check if the post request has the file part
        if 'image' not in request.files:
            return jsonify({"error": "No image part"}), 400
            
        file = request.files['image']
        
        # If user does not select file, browser submits empty part
        if file.filename == '':
            return jsonify({"error": "No selected file"}), 400
            
        if file and allowed_file(file.filename):
            # Secure the filename and save to upload folder
            filename = secure_filename(f"esp32_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jpg")
            filepath = os.path.join(app.config['UPLOAD_FOLDER'], filename)
            file.save(filepath)
            
            # Process any additional form data
            weight = request.form.get('weight', 0)
            rfid = request.form.get('rfid', 'unknown')
            
            # Create response data
            image_url = f"/static/uploads/{filename}"
            
            # Broadcast to SSE clients
            global latest_esp_data
            latest_esp_data = {
                'image_path': image_url,
                'timestamp': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
                'weight': weight,
                'rfid': rfid
            }
            
            return jsonify({
                "success": True,
                "message": "File uploaded successfully",
                "image_path": image_url
            })
            
        return jsonify({"error": "Invalid file type"}), 400
        
    except Exception as e:
        return jsonify({"error": str(e)}), 500
if __name__ == '__main__':
     app.run(host='0.0.0.0', port=5000, debug=True)