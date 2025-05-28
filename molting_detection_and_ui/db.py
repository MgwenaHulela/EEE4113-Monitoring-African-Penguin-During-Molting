# db.py

import sqlite3
import os
from datetime import datetime

DB_PATH = 'penguin_molting.db'

def init_db():
    """Initialize the database with required tables."""
    
    # Connect to the database (creates it if it doesn't exist)
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # Create penguins table with updated fields
    cursor.execute('''
    CREATE TABLE IF NOT EXISTS penguins (
        rfid TEXT PRIMARY KEY,
        last_weight REAL DEFAULT 0,
        current_molting_status INTEGER DEFAULT 0,
        molting_confidence REAL DEFAULT 0,
        last_detection_time TEXT,
        first_seen TEXT,
        notes TEXT,
        sex TEXT DEFAULT 'unknown',
        stage_name TEXT DEFAULT 'Non-molting',
        daily_change REAL DEFAULT 0,
        health TEXT DEFAULT 'Healthy'
    )
    ''')
    
    # Create detections table with updated fields
    cursor.execute('''
    CREATE TABLE IF NOT EXISTS detections (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        rfid TEXT,
        image_path TEXT,
        detection_time TEXT,
        molting_prediction INTEGER,
        confidence REAL,
        model_version TEXT,
        processed INTEGER DEFAULT 0,
        weight_kg REAL DEFAULT 0,
        stage_name TEXT DEFAULT 'Non-molting',
        daily_change REAL DEFAULT 0,
        health TEXT DEFAULT 'Healthy',
        FOREIGN KEY (rfid) REFERENCES penguins(rfid)
    )
    ''')
    
    # Create environmental data table
    cursor.execute('''
    CREATE TABLE IF NOT EXISTS environmental_data (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        date TEXT,
        temperature REAL DEFAULT 0,
        humidity REAL DEFAULT 0,
        light_level REAL DEFAULT 0,
        pressure REAL DEFAULT 0
    )
    ''')
    
    # For migrating existing database - add missing columns
    tables = [row[0] for row in cursor.execute("SELECT name FROM sqlite_master WHERE type='table'").fetchall()]
    
    # If the tables already exist, check for missing columns
    if 'penguins' in tables:
        # Get existing columns in penguins table
        penguin_columns = [row[1] for row in cursor.execute("PRAGMA table_info(penguins)").fetchall()]
        
        # Add new columns to penguins table if they don't exist
        new_penguin_columns = {
            'stage_name': 'TEXT DEFAULT "Non-molting"',
            'daily_change': 'REAL DEFAULT 0',
            'health': 'TEXT DEFAULT "Healthy"'
        }
        
        for col_name, col_type in new_penguin_columns.items():
            if col_name not in penguin_columns:
                cursor.execute(f"ALTER TABLE penguins ADD COLUMN {col_name} {col_type}")
    
    if 'detections' in tables:
        # Get existing columns in detections table
        detection_columns = [row[1] for row in cursor.execute("PRAGMA table_info(detections)").fetchall()]
        
        # Add new columns to detections table if they don't exist
        new_detection_columns = {
            'weight_kg': 'REAL DEFAULT 0',
            'stage_name': 'TEXT DEFAULT "Non-molting"',
            'daily_change': 'REAL DEFAULT 0',
            'health': 'TEXT DEFAULT "Healthy"'
        }
        
        for col_name, col_type in new_detection_columns.items():
            if col_name not in detection_columns:
                cursor.execute(f"ALTER TABLE detections ADD COLUMN {col_name} {col_type}")
    
    # Commit changes and close connection
    conn.commit()
    conn.close()
    
    print("Database initialized successfully!")

# If this script is run directly, initialize the database
if __name__ == "__main__":
    init_db()
    print(f"Database initialized at {DB_PATH}")