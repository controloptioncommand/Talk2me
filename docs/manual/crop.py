# Нарезает скриншоты теста для руководства: python3 crop.py <папка со скриншотами> <папка img>
import sys
from PIL import Image

src, out = sys.argv[1].rstrip("/") + "/", sys.argv[2].rstrip("/") + "/"


def save(im, name):
    im.convert("RGB").save(out + name + ".jpg", quality=88)


# координаты — в базовом размере окна 1120×760
TOOLBAR = (14, 14, 1106, 66)
PHRASE = (14, 76, 1106, 208)
VIEW = (14, 218, 1106, 454)
PANEL = (14, 464, 1106, 746)

talk = Image.open(src + "ui_rubber_talk.png")
spell = Image.open(src + "ui_rubber_spell.png")
save(talk, "main")
save(Image.open(src + "ui_oxygen_spell.png"), "main_oxygen")
save(talk.crop(TOOLBAR), "toolbar")
save(talk.crop((680, 14, 1106, 66)), "toolbar_right")
save(talk.crop(PHRASE), "phrase")
save(Image.open(src + "ui_rubber_edit.png").crop(PHRASE), "phrase_edit")
save(talk.crop(VIEW), "view_talk")
save(spell.crop(VIEW), "view_spell")
save(Image.open(src + "ui_rubber_scope.png").crop(VIEW), "view_scope")
save(talk.crop(PANEL), "panel_talk")
save(spell.crop(PANEL), "panel_spell")
