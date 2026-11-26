"""waveod Studio as a Windows app (tools/package_studio.py): the interface in its own window, no console, no
Python needed on the machine. `python -m waveod_studio --web` is the same app from the sources."""
import sys

from waveod_studio import webapp

if __name__ == '__main__':
    sys.exit(webapp.main(sys.argv[1:]))
