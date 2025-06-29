import pyrebase
import pandas as pd
import datetime
import joblib
import numpy as np
import schedule
import time

# Firebase Config
firebase_config = {
    "apiKey": "key",
    "authDomain": "..##firebaseapp.com",
    "databaseURL": "https://s3322firebaseio.com/",
    "storageBucket": "smart-dustbin.com"
}

# Initialize Firebase
firebase = pyrebase.initialize_app(firebase_config)
db = firebase.database()


model = joblib.load("rf_model.pkl")  # Replace with your model filename

# Static mappings
def get_weather_code(weather_str):
    weather_map = {"clear": 0, "rainy": 1, "snowy": 2}
    return weather_map.get(weather_str.lower(), 0)

# Prediction Function
def predict_time_to_full(bin_id):
    try:
        bin_data = db.child("Dustbins").child(bin_id).get().val()
        if not bin_data:
            print(f"No data for bin {bin_id}")
            return

        # Sample features — ensure your model was trained with these features
        current_fill = bin_data.get("fillLevel", 30)
        compaction_count = bin_data.get("compactions", 0)
        last_collected = bin_data.get("lastCollectedHours", 10)
        weather = get_weather_code(bin_data.get("weather", "clear"))
        time_now = datetime.datetime.now()
        day_of_week = time_now.weekday()
        hour_of_day = time_now.hour
        event_flag = int(bin_data.get("eventNearby", "0"))

        features = np.array([[current_fill, day_of_week, hour_of_day, weather,
                              last_collected, compaction_count, event_flag]])

        prediction = model.predict(features)
        predicted_hours = round(prediction[0], 2)

        db.child("Dustbins").child(bin_id).child("predictedTimeToFull").set(predicted_hours)
        print(f"[{bin_id}] Predicted time to full: {predicted_hours} hrs")

    except Exception as e:
        print(f"Error in predicting for bin {bin_id}: {e}")

# Loop through all bins
def run_cloud_predictions():
    all_bins = db.child("Dustbins").get().val()
    if not all_bins:
        print("No dustbins found in Firebase.")
        return
    for bin_id in all_bins:
        predict_time_to_full(bin_id)

# Schedule to run every 30 minutes
schedule.every(30).minutes.do(run_cloud_predictions)

print("Cloud Layer Analytics started...")
while True:
    schedule.run_pending()
    time.sleep(10)