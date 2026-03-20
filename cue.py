import re
def tab_all_lines(strn, number=1):
    tab = '\t' * number
    result = tab + strn
    result = result.replace('\n', '\n' + tab)
    return result

def space_all_lines(strn, number=4):
    result = tab_all_lines(strn, number)
    result = result.replace('\t', ' ')
    return result
    

class CUETimestamp():
    def __init__(self, minutes: int, seconds: int, frames: int):
        self._minutes = minutes % 99
        self._seconds = seconds % 60
        self._frames = frames % 75

    @property
    def minutes(self):
        return self._minutes

    @property
    def seconds(self):
        return self._seconds

    @property
    def frames(self):
        return self._frames

    @minutes.setter
    def minutes(self, m):
        self._minutes = m % 99

    @seconds.setter
    def seconds(self, s):
        self._seconds = s % 60

    @frames.setter
    def frames(self, f):
        self._frames = f % 75

    def __str__(self):
        return f"{str(self.minutes).zfill(2)}:{str(self.seconds).zfill(2)}:{str(self.frames).zfill(2)}"

    def __repl__(self):
        return self.__str__()
    
    def is_zero(self):
        return self.minutes == 0 and self.seconds == 0 and self.frames == 0

    @classmethod
    def from_string(cls, strn):
        # Creates an instance of CUETimestamp from a string like "00:00:00"
        parts = [int(i) for i in strn.strip().split(":")]
        return cls(parts[0], parts[1], parts[2])



class CUESheet():
    def __init__(self, **kwargs):
        self.audiofiles = kwargs.get("audiofiles", [])

    def render(self):
        sheet = ""
        track_num = 1 # track indexes start at 1
        for f in self.audiofiles:
            sheet += f'FILE "{f.filename}" BINARY\n'
            sheet += space_all_lines(f.render(track=track_num)) + '\n'
            track_num += len(f.tracks)
        
        # Remove long stretches of spaces
        return re.sub(r'^\s*$', '', sheet, flags=re.MULTILINE).replace('\n\n', '\n')


class CUEAudioFile():
    def __init__(self, **kwargs):
        self.filename: str = kwargs.get("filename", "")
        self.tracks: [CUETrack] = kwargs.get("tracks", [])

    def render(self, **kwargs):
        result = "" 
        track_num = kwargs.get('track', 1) # starting track
        for t in self.tracks:
            result += f"TRACK {str(track_num).zfill(2)} AUDIO\n"
            result += space_all_lines(t.render()) + '\n'
            track_num += 1
        return result


class CUETrack():
    def __init__(self, **kwargs):
        self.start_time: CUETimestamp = kwargs.get("start_time",CUETimestamp(0,0,0))
        self.pregap: CUETimestamp = kwargs.get("pregap",CUETimestamp(0,0,0))
    
    def render(self):
        result = ""

        pregap = self.pregap or CUETimestamp(0,0,0)
        print(pregap, pregap.is_zero())

        if not pregap.is_zero():
            result += f"INDEX 00 {pregap}\n"
        result += f"INDEX 01 {self.start_time}\n"
        return result

