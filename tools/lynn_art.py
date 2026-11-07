#!/usr/bin/env python3
"""Art prompts for the Lynn stage page (full-body, 64x64 grid, shown at 3x).

Keys are drawn from the canonical idle sprite; in-betweens are drawn from their key so the
character, scale and feet line stay locked. Run with --phase keys|inbetweens to write a
batch file for ~/.claude/skills/image-gen/scripts/batch.sh. Outputs land in build/artgen/stage3/out
and are cleaned into assets/images/lynn_stage/ by tools/gen_lynn_stage.py.
"""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'build' / 'artgen'
OUT = ART / 'stage3' / 'out'

STYLE = (
    "Authentic low-resolution PIXEL ART sprite of the chibi girl Lynn from the attached references, drawn on a strict "
    "64x64 pixel grid and upscaled 16x with nearest-neighbor (every pixel a perfectly square hard-edged block, no "
    "anti-aliasing, no gradients, no blur). 1px dark outline (#2a2b31), flat cel shading with at most 2 tones per "
    "material, limited 16-colour palette. Character traits that must match: short bob of silver-white hair (#eef0f5, "
    "shade #c9cedb) with light-blue tips (#5bb7de), big blue eyes (#4aa3db), thin round gold glasses (#c8a466), dark "
    "charcoal beret (#2a2b31), small blue bow and X-shaped hair clip, white sailor blouse with light-blue ribbon "
    "(#67b8d8), navy pleated skirt (#2e3b5a). Chibi proportions (head about half the body). IMPORTANT: the background "
    "must be fully TRANSPARENT, no ground shadow, no floor, no frame, no text."
)
KEY = (
    "Match the FIRST attached image exactly: same 64x64 pixel grid upscaled 16x, same canvas size, the SAME character "
    "scale (head, body and legs exactly as large as in the FIRST image), feet on the same ground line, same outline and "
    "palette. The pose must be a clear, readable full-body silhouette change from the FIRST image. Pose: "
)
INB = (
    "The next drawing of a frame-by-frame pixel-art animation, based on the FIRST attached image. Redraw the exact same "
    "character, pixel-art style, palette, outline, canvas size, scale and position (feet on the same ground line; strict "
    "pixel grid, nearest-neighbor, no anti-aliasing, transparent background). Change ONLY this: "
)

# name -> (reference drawing or 'idle', description). Keys reference 'idle'; in-betweens reference their key.
DRAWINGS = {
    # idle activities
    'sweep_1': ('idle', 'sweeping the floor with a small straw broom held in both hands, broom head on the floor to the viewer-left, body leaning slightly left, humming with closed eyes.'),
    'sweep_2': ('sweep_1', 'the broom has swept across to the viewer-right side, body leaning slightly right, eyes open, small smile.'),
    'plane_1': ('idle', 'holding a white paper airplane in her raised right hand above and behind her head, about to throw it, other arm forward for balance, looking up and to the viewer-right, excited.'),
    'plane_2': ('plane_1', 'her throwing arm has swung forward and points up to the viewer-right, the hand is now EMPTY (the plane is gone), body leaning forward, delighted open mouth.'),
    'plane_3': ('plane_1', 'hand EMPTY, holding one hand flat above her eyes like a visor, watching something far up to the viewer-right, happy smile.'),
    'doodle_1': ('idle', 'holding a LARGE open white sketchbook up in front of her chest with her left arm (the book clearly visible, covering part of her blouse) and drawing in it with a big pencil in her right hand, looking down at the page, focused.'),
    'doodle_2': ('doodle_1', 'the pencil has moved to the other side of the page, she sticks the tip of her tongue out while concentrating.'),
    'doodle_3': ('doodle_1', 'she turns the sketchbook around to face the viewer with both hands, showing a simple cute doodle of a cat face on the page, proud closed-eye smile.'),
    # interactions
    'shy_1': ('idle', 'shy: both hands pressed on her blushing cheeks, head turned away to the viewer-left, eyes glancing down, big pink blush.'),
    'shy_2': ('shy_1', 'her body twists to the other side and her head turns to the viewer-right, eyes squeezed shut, blush even stronger, little swaying motion.'),
    'pout_1': ('idle', 'pouting: arms crossed over her chest, cheeks puffed up, looking away to the viewer-right with a grumpy frown, small red anger mark near her head.'),
    'pout_2': ('pout_1', 'she stomps one foot (that knee raised), fists clenched down at her sides, cheeks still puffed, eyes shut tight in a cute tantrum.'),
    'cry_1': ('idle', 'crying: rubbing both eyes with her fists, shoulders hunched, small tears flying from her eyes, wobbly mouth.'),
    'cry_2': ('cry_1', 'fists lowered to her sides, face tilted up, mouth wide open wailing, two streams of tears running down her cheeks.'),
    'laugh_1': ('idle', 'laughing: eyes closed in happy arcs, one hand covering her mouth, shoulders raised, body leaning back a little.'),
    'laugh_2': ('laugh_1', 'bent forward holding her belly with both arms, laughing hard with mouth open, eyes squeezed shut, a tiny tear of laughter.'),
    'bow_1': ('idle', 'polite deep bow: upper body bent forward about 45 degrees, head down, both hands together in front of her skirt, beret still on.'),
    'bow_2': ('bow_1', 'rising from the bow halfway: upper body only slightly bent, head coming up, gentle smile, hands still together in front.'),
    'heart_1': ('idle', 'hugging a big plump red pixel heart (a soft heart-shaped cushion about the size of her head) against her chest with both arms, cheek leaning on it, eyes closed happily, pink blush.'),
    'heart_2': ('heart_1', 'she holds the same big red heart cushion out toward the viewer with both arms extended forward at chest height, offering it, eyes open with a warm smile and blush, a tiny sparkle beside the heart.'),
    'thumbs_1': ('idle', 'confident thumbs up: right arm extended forward with thumb up, left hand on her hip, big confident grin, eyes open.'),
    'thumbs_2': ('thumbs_1', 'both arms forward with both thumbs up, eyes closed in a big smile, little sparkle beside her.'),
    'peace_1': ('idle', 'V peace sign with her right hand held beside her face, winking with one eye, playful tongue out, left hand on hip.'),
    'peace_2': ('peace_1', 'double peace signs with both hands held in front near her face, both eyes closed in a big smile, knees slightly bent.'),
    'salute_1': ('idle', 'standing straight at attention, right hand saluting with fingertips at the edge of her beret, left arm straight down, determined cheerful smile.'),
    'clap_1': ('idle', 'about to clap: hands apart in front of her chest, palms facing each other, excited open smile.'),
    'clap_2': ('clap_1', 'hands pressed together in a clap in front of her chest, eyes closed happily, small pixel impact sparkle between hands.'),
    'shrug_1': ('idle', 'shrugging: shoulders up, elbows bent, both palms turned up and out to the sides, eyebrows raised, awkward half smile.'),
    'facepalm_1': ('idle', 'facepalm: right hand covering her eyes and forehead, head tilted down, other arm hanging, mouth in a flat sigh.'),
    'point_1': ('idle', 'pointing to the viewer-right with her arm fully extended sideways at shoulder height and index finger out, other hand on her hip, body turned slightly, playful smug grin.'),
    'spin_side': ('idle', 'seen in exact side profile facing the viewer-right (turned 90 degrees), arms relaxed, mid-turn, skirt flaring slightly.'),
    'spin_back': ('idle', 'seen from directly BEHIND (back view): back of her beret with its small blue ribbon, back of her silver bob with light-blue tips, back of the sailor collar, skirt, arms relaxed.'),
    'dance_1': ('idle', 'dancing: right arm raised up high, left arm bent down, left leg kicked out to the side, big smile, eyes closed.'),
    'dance_2': ('dance_1', 'the mirrored dance pose: left arm raised up high, right arm bent down, right leg kicked out to the other side, big smile.'),
    'dance_3': ('dance_1', 'both arms raised up above her head, standing on tiptoes mid-twirl, skirt flaring out, joyful open smile.'),
    'scared_1': ('idle', 'scared: hugging herself with both arms, knees together, eyes wide with tiny pupils, pale face, small blue sweat drops beside her head, trembling.'),
    'sneeze_1': ('idle', 'about to sneeze: head tilted back, eyes squeezed, nose scrunched, mouth half open, one hand rising toward her face.'),
    'sneeze_2': ('sneeze_1', 'sneezing forward: head and body jerked forward, eyes shut, both hands in front of her mouth, a tiny puff cloud in front of her face.'),
    'snack_1': ('idle', 'holding a small white rice ball (onigiri with a dark seaweed strip) with both hands near her mouth, eyes sparkling hungrily.'),
    'snack_2': ('snack_1', 'taking a big bite of the rice ball, cheeks puffed and chewing, eyes closed in bliss, a bite missing from the rice ball.'),
    'wink_1': ('idle', 'playful wink: one eye closed, index finger held to her lips in a "shh" gesture, other hand behind her back, cheeky smile.'),
    'sigh_1': ('idle', 'tired sigh: shoulders slumped, arms hanging limp, head tilted down, half-closed droopy eyes, small puff of breath, a dark gloom line above her head.'),
}


def write_jobs(phase: str) -> Path:
    OUT.mkdir(parents=True, exist_ok=True)
    jobs = OUT.parent / f'jobs_{phase}.tsv'
    lines = []
    for name, (ref, desc) in DRAWINGS.items():
        is_key = ref == 'idle'
        if (phase == 'keys') != is_key:
            continue
        if (OUT / f'{name}.png').exists():
            continue
        if is_key:
            refs = f"{ART / 'out' / 'lynn_idle.png'},{ART / 'ref_chibi.png'},{ART / 'ref_front.png'}"
            prompt = f'{STYLE} {KEY}{desc}'
        else:
            refs = f"{OUT / (ref + '.png')},{ART / 'ref_chibi.png'}"
            prompt = f'{INB}{desc}'
        lines.append(f'{name}.png\t{refs}\tt\t{prompt}')
    jobs.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'{jobs}: {len(lines)} jobs')
    return jobs


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--phase', choices=['keys', 'inbetweens'], required=True)
    write_jobs(ap.parse_args().phase)
