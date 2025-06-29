import paho.mqtt.client as mqtt
import time
import pyrebase

# Firebase configuration
# WARNING: In production, move these credentials to environment variables or config file
# Do not commit API keys to version control
config = {
    "apiKey": "xxxxxxyourapikey",
    "authDomain": "smart-dustbin-539ee",
    "databaseURL": "https://smart-dustbin-539ee-default-rtdb.firebaseio.com",
    "storageBucket": "smart-dustbin-539ee.appspot.com"
}

dustbin_original_level = 100
firebase = pyrebase.initialize_app(config)
database = firebase.database()

# MQTT Settings
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
TOPIC_COMMANDS = "smartdustbin/commands"
TOPIC_RESPONSES = "smartdustbin/responses"

# Global variables for MQTT
response_received = None
client = mqtt.Client()

def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker with result code "+str(rc))
    client.subscribe(TOPIC_RESPONSES)

def on_message(client, userdata, msg):
    global response_received
    response_received = msg.payload.decode()
    print(f"Received: {response_received}")

def send_command(command, timeout=2):
    global response_received
    response_received = None
    
    try:
        client.publish(TOPIC_COMMANDS, command)
        print(f"Sent command: {command}")
        
        start_time = time.time()
        while time.time() - start_time < timeout:
            if response_received is not None:
                print(f"Received response: {response_received}")
                return response_received
            time.sleep(0.1)
        
        print(f"Timeout waiting for response to command: {command}")
        return None
    except Exception as e:
        print(f"Error sending command {command}: {e}")
        return None

def get_ultra_data():
    try:
        response = send_command("get_ultra")
        if response and response.isdigit():
            distance = int(response)
            database.child("WasteLevel").set(distance)
            return distance
        else:
            print(f"Invalid ultrasonic response: {response}")
            return None
    except Exception as e:
        print(f"Error getting ultrasonic data: {e}")
        return None

def get_gas_data():
    try:
        response = send_command("get_gas")
        if response and response.isdigit():
            gas_level = int(response)
            database.child("GasLevel").set(gas_level)
            return gas_level
        else:
            print(f"Invalid gas sensor response: {response}")
            return None
    except Exception as e:
        print(f"Error getting gas data: {e}")
        return None

def get_ir_status():
    response = send_command("get_ir", timeout=1.5)
    if response in ["Detected", "Not Detected"]:
        return response
    return "Not Detected"

def lid_control():
    ir_status = get_ir_status()
    print(ir_status)
    if ir_status == "Detected":
        send_command("open_lid")
        print("Lid Opened")
    else:
        send_command("close_lid")
        print("Lid Closed")

def register_user_to_firebase(user_id, name):
    user_data = {
        "name": name,
        "voted": False
    }
    database.child("users").child(user_id).set(user_data)
    print(f"User {user_id} registered in Firebase with name: {name}")

def arduino_register_command(user_id):
    max_attempts = 3
    attempts = 0
    
    while attempts < max_attempts:
        print(f"Registration attempt {attempts + 1} for user: {user_id}")
        response = send_command(f"register{user_id}", timeout=10)
        print(f"Response: {response}")
        
        if response == "registration_success":
            print(f"Fingerprint for User ID: {user_id} successfully registered.")
            return True
        elif response == "registration_failed":
            attempts += 1
            if attempts < max_attempts:
                print(f"Registration failed. Retrying... ({max_attempts - attempts} attempts remaining)")
                time.sleep(2)
        else:
            print("No response or unexpected response during registration")
            attempts += 1
            time.sleep(1)
    
    print(f"Registration failed after {max_attempts} attempts")
    return False

def clear_users():
    response = send_command("clear_all_users")
    print(response)

def verify_fingerprint():
    max_attempts = 5
    attempts = 0
    
    while attempts < max_attempts:
        response = send_command("verify", timeout=3)
        print(f"Verify attempt {attempts + 1}: {response}")
        
        if response == "approved":
            print("Fingerprint verified successfully")
            send_command("open_lid")
            print("User verified - lid opened")
            database.child("Dustbin/verify").set("False")
            return True
        elif response == "denied":
            print("Fingerprint not recognized")
            attempts += 1
            if attempts < max_attempts:
                print(f"Try again ({max_attempts - attempts} attempts remaining)")
                time.sleep(2)
        else:
            print("No response or error during verification")
            attempts += 1
            time.sleep(1)
    
    print("Fingerprint verification failed after maximum attempts")
    return False

def UV_LED():
    response = send_command("uv_led", timeout=10)
    if response == "sterilised":
        return True
    return False

def bin_status_update(status):
    try:
        if status == "normal":
            database.child("Dustbin/Status").set("Dustbin Full. Please Collect!")
        elif status == "biohazard":
            database.child("Dustbin/Status").set("Dustbin is biohazardous. Please Collect!")
            verify = database.child("Dustbin/verify").get().val()
            if verify == "True":  # String comparison
                print("Verifying fingerprint...")
                return verify_fingerprint()
        
        # Check if waste has been collected
        collection_status = database.child("DustbinStatus").get().val()
        if collection_status != "Waste Collected":
            print("Please collect waste!!")
            return True  # Continue monitoring
        else:
            print("Waste collected, starting UV sterilization...")
            sterilization_result = UV_LED()
            if sterilization_result:
                print("Sterilization complete")
                database.child("DustbinStatus").set("Ready for use")
            return sterilization_result
    except Exception as e:
        print(f"Error in bin status update: {e}")
        return True

def compaction():
    response = send_command("compaction", timeout=20)
    if response == "Compaction done":
        return True
    return False

# Initialize MQTT client
client.on_connect = on_connect
client.on_message = on_message
client.connect(MQTT_BROKER, MQTT_PORT, 60)
client.loop_start()

try:
    send_command("close_lid")
    print("Start. (CTRL + C to Exit.)")
    
    # Initialize status variable
    status = "normal"
    
    while True:
        try:
            register_request = database.child("users/register").get().val()
            if register_request == "true":
                user_id = database.child("users/next_user_id").get().val()
                user_name = database.child("users/next_user_name").get().val()

                if user_id and user_name:
                    if arduino_register_command(user_id):
                        register_user_to_firebase(user_id, user_name)
                        database.child("users/register").set("false")
                        print("Registration complete.")
                    else:
                        print("Fingerprint registration unsuccessful. Aborting...")
                else:
                    print("User ID or Name missing in Firebase.")
        except Exception as e:
            print(f"Error during registration: {e}")
        
        try:
            flag2 = bin_status_update(status)
            compactionflag = True
            
            while flag2:
                try:
                    lid_control()
                    dist = get_ultra_data()
                    gaslevel = get_gas_data()
                    print(f"Gas Level: {gaslevel}, Distance: {dist}")
                    
                    if gaslevel and gaslevel >= 400:
                        send_command("close_lid")
                        flag2 = False
                        status = "biohazard"
                    else:
                        if dist and dist < 10:
                            send_command("close_lid")
                            time.sleep(2)
                            if compactionflag:
                                status = compaction()
                            else:
                                status = "normal"
                                flag2 = False
                            
                            x = dustbin_original_level - dist
                            if status:
                                dist = get_ultra_data()
                                print(f"Distance after compaction: {dist}")
                                if dist:
                                    y = dustbin_original_level - dist
                                    efficiency = (x/y)*100 if y != 0 else 0
                                    print(f"Compaction efficiency: {efficiency}%")
                                    if efficiency < 8:
                                        compactionflag = False
                                        print("Compaction efficiency low, disabling compaction")
                                    else:
                                        compactionflag = True
                                    if dist < 10:
                                        status = "normal"
                                        flag2 = False
                                    else:
                                        database.child("DustbinStatus").set("Dustbin Not Full")
                        else:
                            # Distance >= 10, dustbin not full
                            database.child("DustbinStatus").set("Dustbin Not Full")
                    
                    # Check for manual controls
                    mancom = database.child("ManualControl").get().val()
                    if mancom == "Compaction":
                        status = compaction()
                        database.child("ManualControl").remove()
                    elif mancom == "OpenLid":
                        send_command("open_lid")
                        database.child("ManualControl").remove()
                    elif mancom == "CloseLid":
                        send_command("close_lid")
                        database.child("ManualControl").remove()
                        
                except Exception as e:
                    print(f"Error in main loop: {e}")
                    time.sleep(1)
                    
        except Exception as e:
            print(f"Error in bin status update: {e}")
        
        time.sleep(1.5)

except KeyboardInterrupt:
    print("Exit.")
    client.loop_stop()
    client.disconnect()
