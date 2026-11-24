"""Start or stop the local Passport console independently of Terminal."""
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parent
URL = 'http://127.0.0.1:18766'

def running():
    try:
        with urllib.request.urlopen(URL + '/health', timeout=1) as response:
            return json.load(response).get('app') == 'passport-companion-console'
    except (OSError, ValueError):
        return False

def main():
    if sys.argv[1:] == ['stop']:
        if not running():
            print('控制台已停止。')
            return
        with urllib.request.urlopen(URL, timeout=3) as response:
            page = response.read().decode()
        token = re.search(r'name="passport-token" content="([^"]+)"', page).group(1)
        request = urllib.request.Request(URL + '/api/exit', data=b'{}',
            headers={'X-Passport-Token': token, 'Content-Type': 'application/json'})
        urllib.request.urlopen(request, timeout=15).close()
        for _ in range(30):
            if not running():
                print('控制台已停止。')
                return
            time.sleep(.2)
        raise RuntimeError('控制台仍在退出，请稍后重试。')
    if not running():
        directory = ROOT / '.local-console'
        directory.mkdir(exist_ok=True)
        with (directory / 'console.log').open('ab') as log:
            process = subprocess.Popen([str(ROOT / '.venv-console/bin/python'),
                str(ROOT / 'tools/passport_console.py'), '--port', '18766',
                '--config-dir', str(directory), '--no-browser'], cwd=ROOT,
                stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                start_new_session=True)
        for _ in range(50):
            if running():
                break
            if process.poll() is not None:
                raise RuntimeError('启动失败，请查看 .local-console/console.log。')
            time.sleep(.2)
        else:
            raise RuntimeError('启动超时，请查看 .local-console/console.log。')
    subprocess.run(['open', URL], check=True)
    print('控制台已在后台运行，可以关闭此窗口。')

if __name__ == '__main__':
    main()
