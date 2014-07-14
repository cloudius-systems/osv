import SimpleHTTPServer as http
import SocketServer
import subprocess
import os

METADATA_IP = '169.254.169.254'
port = 80

def teardown_rules():
    try:
        subprocess.check_call(['ip', 'addr', 'del', METADATA_IP + '/32', 'scope', 'link', 'dev', 'lo'])
    except:
        pass

    existing_rules = subprocess.check_output(['iptables', '-t', 'nat', '-L', 'PREROUTING', '--line-numbers', '-vn'])
    lines = existing_rules.split('\n')
    lines.reverse()
    for line in lines:
        if METADATA_IP in line:
            line_nr = line.split()[0]
            print("Removing rule: " + line)
            subprocess.check_call(['iptables', '-t', 'nat', '-D', 'PREROUTING', str(line_nr)])

def setup_rules():
    subprocess.check_call(['ip', 'addr', 'add', METADATA_IP + '/32', 'scope', 'link', 'dev', 'lo'])
    subprocess.check_call(['iptables', '-t', 'nat', '-A', 'PREROUTING', '-s', '0.0.0.0/0',
        '-d', METADATA_IP + '/32', '-p', 'tcp', '-m', 'tcp', '--dport', str(port),
        '-j', 'REDIRECT', '--to-ports', '%d' % port])

def start_server(path):
    setup_rules()
    try:
        os.chdir(path)
        handler = http.SimpleHTTPRequestHandler
        server = SocketServer.TCPServer(("", port), handler)

        print("Serving metadata at local port " + str(port))
        server.serve_forever()
    finally:
        teardown_rules()
