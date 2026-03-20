# -- Init -- #

from random import randint, choice
from vibscore import render
import requests, time, sys, os, datetime, re
from cue import *
import discord
import discord.app_commands

# Get tokens
tokens = open("tokens.txt", "r").read().split("\n")
reg_token = tokens[0]  # Regular token is on first line
dev_token = tokens[1]  # Developer token is on second line

# Set version number
version_num = "2.2.1"

# Set help text
help_text = """

Thank you for adding Vib-RiBot (Discord edition)
This is loosely based off of the former Twitter bot (@VibRibot) by Zeriben
Problems? Tell @playnow.bsky.social on Bluesky

Press the "/" key for the slash command menu!

Commands:
---
`!vib` - Vib-Ribbon quote
`!vib scream` - Vibri screams
`!vib cry` - Vibri cries
`!vib growl` - Vibri growls
`!vib slap` - Slap Vibri
`!vib hug` - Hug Vibri
`!vib check` - Vibri does a vib check
`!vib nus` - nus!
`!vib stroke` - Vibri has a stroke reading the above text and fudging dies
`!vib say` - Vibri says what you want her to say!
`!vib rate` - Vibri rates your meme, poem, art piece, or any attachment!
`!vib number <n>` Vibri translates a base 10 number (<n>) into Vib-Ribbon shapes!
`!vib tpose` Vibri T-poses
`!vib hon` Vibri posts a random page of Vibrihon
`!vib cue <filename> <instruction>` - Creates a CUE file
> **<filename>** should be in quotes, e.g. "Song.wav"
> **<instruction>** can be one of two things:
> \\*<n> or %<timestamps>
>     \\*<n> repeats the song <n> times, for example, 
>         `!vib cue "Song.wav" *10`
>     will give a cue file that has Song.wav listed 10 times
> 
>     %<timestamps> works like this:
>     Say you have a file that is structured like this:
>     
>     Track 1: 00:00:00 - 01:00:00
>     Track 2: 01:00:00 - 01:59:00
>     Track 3: Pregap 01:59:00; 02:00:00 - end of disc
> 
>     You would enter:
>        `!vib cue "test song.wav" %00:00:00|01:00:00|01:59:00?02:00:00`
>     
>     **%**XX:XX:XX - beginning of track list
>     **|**XX:XX:XX - next track
>     **|**PP:PP:PP**?**XX:XX:XX - track with pregap
---

"""

# Get quotes
quotes = open("quotes.txt", "r").read().split("\n")

# Get ratings
ratings = open("ratings.txt", "r").read().split("\n")

# Get slurs
slurs = open("slurs.txt", "r").read().split("\n")

# Developer mode toggle
try:
    with open('dev', 'r') as f:
        if f.read().strip().lower() == "false":
            dev_mode = False
        else:
            dev_mode = True
except FileNotFoundError:
    dev_mode = False

# Set token for given mode
if dev_mode:
    token = dev_token
    print('Dev Mode ON')
else:
    token = reg_token

# Image urls
images = {
    "christmas": "https://cdn.discordapp.com/attachments/760576449387954229/791703944799059988/video.mp4",
    "nus": "https://cdn.discordapp.com/attachments/587087600360226817/763503886622654494/Screen_Shot_2020-10-07_at_4.48.10_PM.png",
    "stroke": "https://cdn.discordapp.com/attachments/587066858239295491/762884846196359189/unknown.png",
    "tpose": "https://cdn.discordapp.com/attachments/760576449387954229/833772562357420042/vibri_t-pose.png",
    "vib_check_pass": "https://cdn.discordapp.com/attachments/760576449387954229/782784397178699786/vib_check_pass.png",
    "vib_check_fail": "https://cdn.discordapp.com/attachments/760576449387954229/782784230521438238/vib_check_fail.png",

}

# Vibrihon image urls (loaded from file)
hon = {}

# Load vibrihon urls
print("Loading vibrihon urls")
with open("hon.txt", "r") as hf:
    h = hf.read().split("\n")
    for i in h:
        l = i.split(";")
        hon[l[0]] = l[1]

print("Loaded!")

# Phrases
phrases = {
    "growl": "Grrrrrrrggh!!",
    "scream": "Aahhhhhhhhh!",
    "cry": "Waah! Waah!"
}


# -- Functions -- #

# Update vibrihon database
def update_hon():
    f = open("hon.txt", 'w')
    txt = ""
    for i in hon:
        txt += i + ";" + hon[i] + "\n"
    f.write(txt[:-1])
    print("Updated database")


# Send a quote
def quote():
    return choice(quotes) or "tfw you miss a block :pensive:"  # The last part is an error handler


# Hug Vibri
def hug_react():
    rct = [
        "Yay!",
        "Haha!",
        "Happy!",
        "pog"
    ]
    return choice(rct)


# Roulette
def roulette(good, bad, chance=5):
    chamber = randint(1, chance)
    if chamber == 1:
        return bad
    return good


# Rate
def rate():
    v = choice(ratings)
    r = choice(v.split("[")[1].split(","))  # rating text[0,1,2,3
    v = v.split("[")[0]
    return v + " " + r + "/10"


# Slap
def slap_react():
    reactions = [
        "Ouch!",
        "Owie!",
        "Eeek!",
        "Aahhhhhhhhh!"
    ]
    return choice(reactions)


# Execution
def execution_countdown():
    today = datetime.date.today()
    future = datetime.date(2021, 7, 2)
    diff = future - today
    if (diff.days == 0):
        return "Vibri will be publicly executed today."
    elif (diff.days < 0):
        return "Vibri was publicly executed " + str(abs(diff.days)) + " days ago."
    else:
        return "Vibri will be publicly executed in " + str(diff.days) + " days"


# !vib say function
def vib_say(m, usingSlash):
    for i in m.split(" "):
        if i.lower() in slurs and i != "":
            return "Grrrrrrrggh!!"

    if usingSlash:
        return m  # If using slash commands, no need to crop anything out
    return m[9:]  # Used to crop out "!vib say"


# !vib cue functions

def old_cue(m):
    # Old cue function

    # Parse arguments
    cue_inst = str(m).split(" ")[2:] # !vib cue [here...]
    ct = " ".join(cue_inst)
    file = ct.split("\"")[1] # !vib cue "[here]"
    cue_inst = " ".join(cue_inst).split('"' + file + '"') # !vib cue "filename.bin" [here...]
    ret = ""

    # *<number>
    if cue_inst[1].startswith(" *"):
        count = int(cue_inst[1].split("*")[1]) or 1
        for i in range(1, count + 1):
            if cue_zero_pad(i) == "ERR": # cue_zero_pad is a zero-padding function
                return "Game over! Too many tracks."
            
            ret += "FILE \"" + file + "\" BINARY\n"
            ret += "\tTRACK " + cue_zero_pad(i) + " AUDIO\n"
            ret += "\t\tINDEX 01 00:00:00\n"
        
        prod = "```\n" + ret + "\n```"
        
        if len(prod) > 1999:
            return "Game over! The CUE file is too long!"
        return prod

    # %XX:XX:XX|XX:XX:XX...
    elif cue_inst[1].startswith(" %"):
        timestamps = cue_inst[1][2:].split("|") # [%XX:XX:XX, PP:PP:PP?XX:XX:XX, XX:XX:XX]
        ret += "FILE \"" + file + "\" BINARY\n"
        ctr = 0
        for i in timestamps:
            ctr += 1
            ret += "  TRACK " + cue_zero_pad(ctr) + " AUDIO\n"
            if "?" in i:
                l = i.split("?")
                ret += "    INDEX 00 " + l[0] + "\n"
                ret += "    INDEX 01 " + l[1] + "\n"
            else:
                ret += "    INDEX 01 " + i + "\n"
        prod = "```\n" + ret + "\n```"
        
        if len(prod) > 1999:
            return "Game over! The CUE file is too long."
        return prod
    else:
        return "Game over! Invalid argument '" + cue_inst[1][0] + "'"

# The regex for the whole argument
cue_arg_re = re.compile(
    r'!vib cue\s+[\"“”](?P<name>.*)[\"“”]\s+(?P<args>(\*\s*\d{1,2})|([%\|?]\d{1,2}:\d{1,2}:\d{1,2})+)'
    )
# The regex to sort out chunks of the argument
chunk_re = re.compile(r'(([%\|]\d{1,2}:\d{1,2}:\d{1,2})(\?\d{1,2}:\d{1,2}:\d{1,2})?)')
def cue(message):
    mtch = cue_arg_re.match(message)

    name = mtch.group('name')
    if not name:
        return "Game over! Couldn't detect a filename! Make sure you're surrounding the filename with quotation marks!"
    
    args = mtch.group('args')
    if not args:
        return "Game over! Couldn't detect a valid argument structure! Type !vib help for an example"
    
    if args.startswith('*'):
        args = args.replace('*', '').strip()
        repeat_times = int(args)

        if repeat_times > 99:
            return "Game over! Too many tracks!"

        sheet = CUESheet()
        audiofile = CUEAudioFile(filename=name, tracks=[CUETrack()])
        sheet.audiofiles = [audiofile] * repeat_times

        return sheet.render()
    
    elif args.startswith('%'):
        # get the groups matched and take off the first character (either %, |, or ?)
        chunks = [(m[1][1:], m[2][1:]) for m in chunk_re.findall(args)]

        # get the tracks from the arguments
        tracks = []
        for chunk in chunks:
            start = CUETimestamp.from_string(chunk[0])
            pregap = None
            if chunk[1]: # switch the first and last timestamp around if there is a question mark
                pregap = CUETimestamp.from_string(chunk[0])
                start = CUETimestamp.from_string(chunk[1])

            tracks.append(CUETrack(start_time=start, pregap=pregap))
        
        audiofile = CUEAudioFile(filename=name, tracks=tracks)
        sheet = CUESheet(audiofiles=[audiofile])

        return sheet.render()

    else:
        return "Game over! Something is wrong with your argument..."

def ctry(m):
    try:
        result = cue(m)

        if result.startswith("Game over!"):
            return result
        
        result = f"```{result}```"

        if len(result) >= 2000:
            return "Game over! The resulting CUE file is too long to send!"
        
        return result
    except:
        return "Game over! Something's wrong with your command."


def cue_zero_pad(n):
    if n > 99:
        return "ERR"
    elif n < 1:
        return "01"
    num = str(n)
    if len(num) == 1:
        num = "0" + num
    return num


# !vib rihon

def get_hon():
    pass

# -- Discord initialization -- #

intents = discord.Intents.default()
intents.message_content = True

# Enable slash commands through the client
client = discord.Client(intents=intents)
tree = discord.app_commands.CommandTree(client)
#slash = SlashCommand(client, sync_commands=True)


@client.event
async def on_ready():
    print("Discord says that the bot is ready")
    try:
        synced = await tree.sync()
        print(f"synced {synced} command(s)")
    except Exception as e:
        print(e)
    game = discord.Game("!vib help (v" + version_num + ")")
    await client.change_presence(status=discord.Status.online, activity=game)


# -- Slash commands -- #
# https://discordpy.readthedocs.io/en/stable/interactions/api.html#discord.app_commands.CommandTree
# looks like you have to set this manually

# /vib
@tree.command(name='vib', description="A quote from Vib-Ribbon")
async def _vib(interaction: discord.Interaction):
    await interaction.response.send_message(quote())

# /hug
@tree.command(name="hug", description="Hug Vibri")
async def _hug(interaction: discord.Interaction):
    await interaction.response.send_message(hug_react())

# /vibcheck
@tree.command(name="vibcheck", description="Vibri does a vib check")
async def _vibcheck(interaction: discord.Interaction):
    await interaction.response.send_message(roulette(
        images["vib_check_pass"],
        images["vib_check_fail"],
        5
    ))


# /number
@tree.command(name="number", description="Vib-Ribbon number")
@discord.app_commands.describe(number="The number to convert")
async def _number(interaction: discord.Interaction, number: int):
    rnum = number
    if (rnum < 0):
        rnum = 0
    ts = str(datetime.datetime.now()).replace(":", "-")
    render.render(str(rnum), "render-" + ts)
    await interaction.response.send_message(file=discord.File("render-" + ts + ".jpg"))
    os.remove("render-" + ts + ".jpg")

# /rate
yt_title_re = re.compile(r'<title>(?P<title>.*) - YouTube</title>')
@tree.command(name="rate", description="Send a phrase, or a link to a picture or a YouTube video.")
@discord.app_commands.describe(thing="A text, link to an image, or link to a video")
async def _rate(interaction: discord.Interaction, thing: str):

    # Test if string is a link
    def get_link_type(l):
        ltype = "TEXT"
        if l.startswith("http://") or l.startswith("https://"):
            ltype = "LINK"
            image_links = ['.jpg', '.png', '.jpeg', '.gif']
            for i in image_links:
                if l.endswith(i):
                    return "IMG"
            if "youtu" in l:
                return "YT"
        return ltype

    def download_video_title(url):
        req = requests.get(url)
        if req.status_code != 200:
            return None
        
        text = req.text
        mtch = yt_title_re.search(text)

        title = mtch.group('title')

        return title
        
    # Get YouTube character sequence from link
    def get_video_chars(l):
        if '?v=' in l:
            return l.split("?v=")[1][:11]
        if '.be' in l: #youtu.be
            return l.split('.be/')[1][:11]

    vib_rating = "Vibri says: " + rate()
    em = discord.Embed(title=vib_rating)
    em.description = thing
    thumbnail = ""
    lt = get_link_type(thing)  # Reduce redundancy
    if lt == "IMG":
        em.set_image(url=thing)
    if lt == "YT":  # Only using if statements in case the text type changes...
        try:  # Test if this is a valid YouTube link
            video_id = get_video_chars(thing)
            thumbnail = "https://i.ytimg.com/vi/" + video_id + "/hqdefault.jpg"
            if requests.get(thumbnail).status_code == 404:
                print("Not valid link")
                raise Exception("Not a valid YouTube link")
            
            em.set_image(url=thumbnail)
            em.url = "https://youtube.com/watch?v=" + video_id

            vid_title = download_video_title(thing)
            if vid_title:
                em.description = f"[{vid_title}]({em.url})"
            else:
                em.description = em.url
                
        except:
            lt = "LINK"  # ...like in here
    if lt == "LINK":
        em.url = thing
        em.description = thing
    if lt == "TEXT":
        em.description = thing
    print(thing)
    await interaction.response.send_message(embed=em)

# /slap
@tree.command(name="slap", description="Slap Vibri")
async def _slap(interaction: discord.Interaction):
    await interaction.response.send_message(slap_react())

# Commented out because the PS3 store is not shutting down for now
# /execution
"""
@tree.command(name="execution", description="Countdown to Vibri's public execution on the PS3 store")
async def _execution(interaction: discord.Interaction):
    await interaction.response.send_message(execution_countdown())
"""

# /say
@tree.command(name="say", description="Vibri says anything you want her to!")
@discord.app_commands.describe(phrase="Thing that you want Vibri to say")
async def _say(interaction: discord.Interaction, phrase: str):
    await interaction.response.send_message(vib_say(phrase, True))

# /scream
@tree.command(name="scream", description="Make Vibri scream")
async def _scream(interaction: discord.Interaction):
    await interaction.response.send_message(phrases["scream"])


# /cry
@tree.command(name="cry", description="Make Vibri cry")
async def _cry(interaction: discord.Interaction):
    await interaction.response.send_message(phrases["cry"])


# /stroke
@tree.command(name="stroke", description="Vibri has a stroke and fudging dies")
async def _stroke(interaction: discord.Interaction):
    await interaction.response.send_message(images["stroke"])


# /nus
@tree.command(name="nus", description="nus!")
async def _nus(interaction: discord.Interaction):
    await interaction.response.send_message(images["nus"])


# /growl
@tree.command(name="growl", description="Make Vibri growl.")
async def _growl(interaction: discord.Interaction):
    await interaction.response.send_message(phrases["growl"])


# /tpose
@tree.command(name="tpose", description="Vibri T-Poses")
async def _tpose(interaction: discord.Interaction):
    await interaction.response.send_message(images["tpose"])

# /hon
@tree.command(name="hon", description="A random page from Vibrihon")
async def _hon(interaction: discord.Interaction):
    pg = str(randint(4, 86))
    if not pg in hon:
        await interaction.response.send_message("Vibrihon page " + pg, file=discord.File("hon/vibrihonpg" + str(pg) + ".png"))
    else:
        await interaction.response.send_message(hon[pg])

# /cue
@tree.command(name="cue", description="Generate a CUE file")
@discord.app_commands.describe(filename="The .bin or .wav file the CUE references")
@discord.app_commands.describe(arguments="The arguments to pass into the CUE making function (see !vib help for details)")
async def _cue(interaction: discord.Interaction, filename: str, arguments: str):
    await interaction.response.send_message(ctry(f'!vib cue "{filename}" {arguments}'))

# /cue repeat
@tree.command(name="cue_repeat", description="Generate a CUE file with the same track listed multiple times on the disc for alternate beatmaps")
@discord.app_commands.describe(filename="The .bin or .wav file the CUE references")
@discord.app_commands.describe(repeats="The amount of times to repeat the track")
async def _cue_repeat(interaction: discord.Interaction, filename: str, repeats: int):
    await interaction.response.send_message(ctry(f'!vib cue "{filename}" *{repeats}'))

# -- Legacy commands -- #

# on_message blocks slash commands

@client.event
async def on_message(message):
    global hon

    if message.author == client.user:
        # Update vibrihon page database
        if message.content.startswith("Vibrihon page"):
            pg = message.content.split("page ")[1]
            hon[pg] = message.attachments[0].url
            update_hon()
        # Don't do a command if it comes from the bot itself
        return

    msg = message.content.lower().strip()  # Redundancy

    if msg == "!vib":
        await message.channel.send(quote())

    elif msg == "!vib scream":
        await message.channel.send(phrases["scream"])

    elif msg == "!vib check":
        await message.channel.send(roulette(images["vib_check_pass"], images["vib_check_fail"], 5))

    elif msg == "!vib help":
        await message.channel.send(help_text)

    elif msg.startswith("!vib cue"):
        await message.channel.send(ctry(message.content))

    elif msg == "!vib growl":
        await message.channel.send(phrases["growl"])

    elif msg == "!vib slap":
        await message.channel.send(slap_react())

    elif msg == "!vib cry":
        await message.channel.send(phrases["cry"])

    elif msg == "!vib stroke":
        await message.channel.send(images["stroke"])

    elif msg == "!vib nus":
        await message.channel.send(images["nus"])

    elif msg == "!vib hug":
        await message.channel.send(hug_react())

    elif msg.startswith("!vib say"):
        await message.channel.send(vib_say(msg, False))

    elif msg.startswith("!vib rate"):
        await message.channel.send(rate())

    elif msg.startswith("!vib number"):
        try:
            l = int(msg.split(" ")[2])  # ['!vib', 'number', '(number)']
        except ValueError:
            await message.channel.send("Error: not a number")
            return
        rnum = message.content.split(" ")[2]
        if int(rnum) < 0:  # Don't allow negatives
            rnum = 0
        ts = str(datetime.datetime.now()).replace(":", "-")
        render.render(str(rnum), "render-" + ts)
        await message.channel.send(file=discord.File("render-" + ts + ".jpg"))
        os.remove("render-" + ts + ".jpg")

    elif msg == "!vib stop":
        if dev_mode:
            sys.exit()

    elif msg == "!vib christmas":
        t = datetime.datetime.today()
        if t.month == 12 and t.day < 26:
            await message.channel.send(images["christmas"])
        else:
            await message.channel.send("This command is not available right now.")

    elif msg == "!vib tpose":
        await message.channel.send(images["tpose"])

    elif msg == "!vib hon" or msg == "!vib rihon":
        pg = str(randint(4, 86))
        print(pg)
        if not pg in hon:
            await message.channel.send("Vibrihon page " + pg, file=discord.File("hon/vibrihonpg" + str(pg) + ".png"))
        else:
            await message.channel.send(hon[pg])

    # At one point sending files wasn't working, so I added this command to troubleshoot errors.
    # I simply upgraded discord.py!
    elif msg == "!vib file":
        if dev_mode:
            await message.channel.send(file=discord.File("hon/vibrihonpg4.png"))


# -- Startup assistance -- #

cres = 0

while cres != 200:
    try:
        # Google's a safe bet since it's always on.
        cres = requests.get("http://www.google.com").status_code
    except requests.exceptions.ConnectionError:
        print("Connection failed. Trying again in 10 seconds.")
        time.sleep(10)

print("Vibri has started!")

client.run(token)
