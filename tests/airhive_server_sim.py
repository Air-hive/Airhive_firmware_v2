from flask import Flask, request, jsonify, make_response
from zeroconf import Zeroconf, ServiceInfo
import socket
import argparse

app = Flask(__name__)

CNCM_MAX_COMMAND_SIZE = 128
machine_running = False

@app.route('/test', methods=['GET'])
def test_get():
    if request.content_length and request.content_length > 64:
        return '', 413
    content_type = request.headers.get('Content-Type', 'application/octet-stream')
    data = request.get_data()
    resp = make_response(data, 200)
    resp.headers['Content-Type'] = content_type
    return resp

@app.route('/commands', methods=['POST'])
def commands_post():
    if request.content_length and request.content_length > 50*1024:
        return '', 413
    try:
        body = request.get_json(force=True)
    except Exception:
        return '', 400
    commands = body.get('commands')
    if not isinstance(commands, list):
        return '', 400
    sent_commands = 0
    for cmd in commands:
        if not isinstance(cmd, str) or len(cmd) >= CNCM_MAX_COMMAND_SIZE:
            return '', 400
        # Simulate sending command (always succeeds)
        sent_commands += 1
    return jsonify(sent_commands=sent_commands), 200

@app.route('/responses', methods=['GET'])
def responses_get():
    if request.content_length and request.content_length > 32:
        return '', 413
    # Simulate response data
    responses = 'ok T:30.62 /0.00 B:29.77 /0.00 @:0 B@:0\n----------------\n\nBegin file list\n\nCUBE~1.GCO 324730\n\n3DBENC~1.GCO 2540254\n\nEnd file list\n\n-----------\n\necho:busy: processing\n\necho:busy: processing\n\nX:79.00 Y:98.00 Z:0.65 E:0.00 Count A:14183B:1523 Z:525\n\nok\n------------------------------------\nNot SD printing\n\nok'
    return jsonify(responses=responses), 200

@app.route('/machine-status', methods=['GET'])
def machine_status_get():
    # Simulate always connected
    return jsonify(status="Connected"), 200

@app.route('/start', methods=['PUT'])
def start_put():
    global machine_running
    if machine_running:
        return '', 400
    machine_running = True
    # Simulate always succeeds
    return '', 200

@app.route('/stop', methods=['PUT'])
def stop_put():
    global machine_running
    if not machine_running:
        return '', 400  # Already stopped
    machine_running = False
    # Simulate always succeeds
    return '', 200

@app.route('/clear', methods=['PUT'])
def clear_put():
    global machine_running
    # Simulate always succeeds
    return '', 200

@app.route('/machine-config', methods=['PUT'])
def machine_config_put():
    if request.content_length and request.content_length > 32:
        return '', 413
    try:
        body = request.get_json(force=True)
    except Exception:
        return '', 400
    baudrate = body.get('baudrate')
    if not isinstance(baudrate, int) or baudrate <= 0:
        return '', 400
    # Simulate always succeeds
    return '', 200

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description="Process a single integer argument"
    )
    # Define one positional argument called “number”, cast to int
    parser.add_argument(
        "number",
        type=int,
        help="An integer value to process"
    )
    args = parser.parse_args()
    n = args.number

    info = ServiceInfo(
        type_='_http._tcp.local.',
        name=f'Airhive{n}-test._http._tcp.local.',
        addresses=[socket.inet_aton("127.0.0.1")],
        port=8000 + n,
        server=f'Airhive{n}-test.local.',
        properties={}
    )
    zeroconf = Zeroconf()
    zeroconf.register_service(info, allow_name_change=True)
    print('mDNS service registered.')
    app.run(port=8000 + n, debug=True)
