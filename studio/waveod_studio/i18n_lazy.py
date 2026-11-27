"""Tables of texts that follow the language (docs/i18n.md): a module's table keeps catalog keys and gives each key's
text in the language of the moment it is read (i18n.tr then, never at import time), so the code that indexes it
(the classic app's too) stays as it was.

    KINDS = Texts({0: 'py.empty', 1: 'py.clone_original'})      KINDS.get(k, '?'), KINDS[k]
    NAMES = TextList(('py.lat_route_l', 'py.lat_route_r'))      NAMES[i], len(NAMES), for name in NAMES

Keys as tr() takes them (without "studio."). dict(table) / {**table} copy the keys, not the texts."""
from waveod_studio.i18n import tr


class Texts(dict):
    """{value: catalog key}, read as {value: text}"""

    def __getitem__(self, k):
        return tr(dict.__getitem__(self, k))

    def get(self, k, default=None):
        return self[k] if k in self else default

    def values(self):
        return [self[k] for k in self]

    def items(self):
        return [(k, self[k]) for k in self]


class TextList(tuple):
    """(catalog keys), read as (texts)"""

    def __getitem__(self, i):
        v = tuple.__getitem__(self, i)
        return tuple(tr(x) for x in v) if isinstance(i, slice) else tr(v)

    def __iter__(self):
        return (tr(x) for x in tuple.__iter__(self))

    def __contains__(self, text):
        return any(t == text for t in self)

    def index(self, text, *a):
        return list(self).index(text, *a)
