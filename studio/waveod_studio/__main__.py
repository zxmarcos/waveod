"""python -m waveod_studio            the app
python -m waveod_studio --web      the new interface (webapp.py: Svelte in a pywebview window; Grade, Presets, Bateria)
python -m waveod_studio info       what the pedal says (waveod firmware on USB)
python -m waveod_studio backup F   every preset into a .wvbak file
python -m waveod_studio restore F  a .wvbak back into the pedal
python -m waveod_studio dump F [--all | --start A --length N]   the flash (read only)
python -m waveod_studio latency [L|R|SEND|PRESET]   the pedal's latency test (a cable from that output to
                                   the input; PRESET: through the active preset, no cable)
python -m waveod_studio clone      the names of the 10 Clone slots
python -m waveod_studio nam F.nam --slot N [--name X] [--44k]   a NAM capture into Clone slot N (replaces it)
"""
import sys


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if '--web' in argv:
        from waveod_studio import webapp
        return webapp.main([x for x in argv if x != '--web'])
    if not argv or argv[0].startswith('-'):
        from waveod_studio import app
        return app.main(argv)
    from waveod_studio import cli
    return cli.main(argv)


if __name__ == '__main__':
    sys.exit(main())
