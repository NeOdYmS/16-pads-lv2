"""Generate the port-heavy LV2 description from a single pad definition."""
import argparse
from pathlib import Path
import shutil

URI = "https://github.com/NeOdYmS/16-pads-lv2#sampler"
PREFIXES = """@prefix atom: <http://lv2plug.in/ns/ext/atom#> .
@prefix doap: <http://usefulinc.com/ns/doap#> .
@prefix lv2: <http://lv2plug.in/ns/lv2core#> .
@prefix log: <http://lv2plug.in/ns/ext/log#> .
@prefix midi: <http://lv2plug.in/ns/ext/midi#> .
@prefix mod: <http://moddevices.com/ns/mod#> .
@prefix modgui: <http://moddevices.com/ns/modgui#> .
@prefix patch: <http://lv2plug.in/ns/ext/patch#> .
@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix state: <http://lv2plug.in/ns/ext/state#> .
@prefix urid: <http://lv2plug.in/ns/ext/urid#> .
@prefix units: <http://lv2plug.in/ns/extensions/units#> .
@prefix worker: <http://lv2plug.in/ns/ext/worker#> .
"""


def metadata(mod_gui=False):
    ports = [
        '[ a lv2:InputPort, atom:AtomPort ; lv2:index 0 ; lv2:symbol "events" ; '
        'lv2:name "MIDI / sample commands" ; atom:bufferType atom:Sequence ; '
        'atom:supports midi:MidiEvent, patch:Message ; lv2:designation lv2:control ]',
        '[ a lv2:OutputPort, atom:AtomPort ; lv2:index 1 ; lv2:symbol "notify" ; '
        'lv2:name "Sample notifications" ; atom:bufferType atom:Sequence ; '
        'atom:supports patch:Message ; lv2:designation lv2:control ]',
        '[ a lv2:OutputPort, lv2:AudioPort ; lv2:index 2 ; lv2:symbol "left" ; '
        'lv2:name "Left" ]',
        '[ a lv2:OutputPort, lv2:AudioPort ; lv2:index 3 ; lv2:symbol "right" ; '
        'lv2:name "Right" ]',
    ]
    for pad in range(1, 17):
        for offset, (symbol, name, default, minimum, maximum) in enumerate([
            ("mode", "Mode", 0, 0, 2),
            ("note", "MIDI note (-1 disabled)", -1, -1, 127),
            ("cc", "MIDI CC (-1 disabled)", -1, -1, 127),
            ("gain", "Gain", 0, -60, 12),
            ("trigger", "Trigger (MODEP MIDI mapping)", 0, 0, 1),
        ]):
            extra = ""
            if symbol == "mode":
                extra = (' ; lv2:portProperty lv2:integer, lv2:enumeration ; '
                         'lv2:scalePoint [ rdfs:label "One-shot" ; rdf:value 0 ], '
                         '[ rdfs:label "Loop" ; rdf:value 1 ], '
                         '[ rdfs:label "Trig" ; rdf:value 2 ]')
            elif symbol == "trigger":
                extra = " ; lv2:portProperty lv2:toggled, mod:preferMomentaryOnByDefault"
            elif symbol == "gain":
                extra = " ; units:unit units:db"
            elif symbol != "gain":
                extra = " ; lv2:portProperty lv2:integer"
            ports.append(
                f'[ a lv2:InputPort, lv2:ControlPort ; lv2:index {4 + (pad-1)*6 + offset} ; '
                f'lv2:symbol "{symbol}{pad:02}" ; lv2:name "Pad {pad:02} {name}" ; '
                f'lv2:default {default} ; lv2:minimum {minimum} ; lv2:maximum {maximum}{extra} ]'
            )
        ports.append(
            f'[ a lv2:OutputPort, lv2:ControlPort ; lv2:index {4 + (pad-1)*6 + 5} ; '
            f'lv2:symbol "active{pad:02}" ; lv2:name "Pad {pad:02} activity" ; '
            'lv2:minimum 0 ; lv2:maximum 1 ; lv2:portProperty lv2:toggled ]'
        )
    properties = ", ".join(f"<{URI}:sample{pad:02}>" for pad in range(1, 17))
    gui = """
    modgui:gui [
        modgui:resourcesDirectory <modgui> ;
        modgui:iconTemplate <modgui/icon.html> ;
        modgui:stylesheet <modgui/style.css> ;
        modgui:javascript <modgui/script.js> ;
        modgui:brand "NeOdYmS" ;
        modgui:label "16 Pads"
        ; modgui:monitoredOutputs
        """ + ",\n        ".join(
            f'[ lv2:symbol "active{pad:02}" ]' for pad in range(1, 17)
        ) + """
    ] ;
""" if mod_gui else ""
    text = PREFIXES + f"""
<{URI}> a lv2:Plugin, lv2:InstrumentPlugin ;
    doap:name "16 Pads" ;
    doap:description "16-pad WAV sampler with independent playback modes" ;
    lv2:minorVersion 0 ; lv2:microVersion 1 ;
    lv2:requiredFeature urid:map, worker:schedule ;
    lv2:optionalFeature log:log ;
    lv2:extensionData worker:interface, state:interface ;
    patch:writable {properties} ;
    patch:readable {properties}, <{URI}:error> ;
{gui}
    lv2:port """ + ",\n        ".join(ports) + " .\n"
    for pad in range(1, 17):
        text += (
            f'<{URI}:sample{pad:02}> a lv2:Parameter ; '
            f'rdfs:label "Pad {pad:02} sample" ; rdfs:range atom:Path ; '
            'mod:fileTypes "audiosample,audioloop" .\n'
        )
    text += f'<{URI}:error> a lv2:Parameter ; rdfs:label "Sample error" ; rdfs:range atom:String .\n'
    return text


def generate(directory, mod_gui=False):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "manifest.ttl").write_text(
        PREFIXES + f'<{URI}> a lv2:Plugin ; lv2:binary <sampler.so> ; '
        'rdfs:seeAlso <sampler.ttl> .\n', encoding="utf-8"
    )
    (directory / "sampler.ttl").write_text(metadata(mod_gui), encoding="utf-8")
    if mod_gui:
        shutil.copytree(Path(__file__).resolve().parent.parent / "modgui",
                        directory / "modgui", dirs_exist_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--mod-gui", type=int, choices=(0, 1), default=0)
    args = parser.parse_args()
    generate(args.directory, bool(args.mod_gui))
