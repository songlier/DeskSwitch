<div align="center">  
    
# DeskSwitch  
<p>  
  <img src="./icon.ico" alt="Project Preview" width="200">  
</p>    
多桌面切换器, 使一台显示器比多台显示器更强大    
  
A multi-desktop switcher that makes one monitor more powerful than multiple monitors    
  
[简体中文](./README_CN.md)    
</div>  
  
## Usage    
1. Download `DeskSwitch.zip` from Releases    
2. Extract it and run `DeskSwitch.exe`    
3. Set it to start with Windows: press `Win + R`, type `shell:startup`, and create a shortcut to `DeskSwitch.exe` in the opened folder    
  
## Hotkeys    
`Ctrl+Alt+Esc`: Enable/disable switching    
`Ctrl+Alt+Shift+Esc`: Exit the program    
  
## Trigger Area    
You can configure the trigger area by editing `area.txt`    
The value order is: {left, top, right, bottom}    
Values can be specified in pixels (`px`) or as a percentage of the screen    
  
## Exceptions    
Some programs may block desktop switching and make it unavailable    
You can add such programs to `except.txt`, and the program will switch desktops using the DLL method instead    
Known examples include but are not limited to `1Remote.exe` and `GameViewer.exe`    
  
## Settings    
You can customize the program by editing `conf.txt`    
  
1. Single-corner / Dual-corner Mode    
Set the program mode to manual switching (dual-corner mode) or automatic direction detection (single-corner mode)    
Single-corner mode: only the top-left corner is used as the trigger area; in this mode, the switching direction is determined by the relative position of the previous desktop to the current desktop    
Dual-corner mode: both the top-left and top-right corners are used as trigger areas; the switching direction corresponds to the left/right direction of the corner    
  
2. Single-corner Direction Memory Timeout    
Sets how long the direction memory is kept in single-corner mode. Within this time period, repeated triggers are treated as continuous switching and the direction remains unchanged. Once the timeout is exceeded, the switch is considered complete, and the next switch direction will be adjusted based on the previous desktop position    
  
3. Continuous Switching    
When enabled, keeping the mouse in the trigger corner will switch desktops continuously until the mouse leaves the corner. When disabled, entering the trigger corner will only switch once    
  
4. Continuous Switching Interval    
The interval between automatic switches when continuous switching is enabled    
  
5. Auto Reverse Switching    
When the current switching direction has already reached the last desktop, determines whether the direction should automatically reverse    
  
6. Trigger on Leave    
Off: switching is triggered when the mouse enters the trigger corner    
On: switching is triggered only when the mouse leaves the trigger corner. Releasing the mouse button in the trigger corner (for example, when dragging a window to the corner) or pressing the mouse button in the trigger corner (for example, clicking a window close button in the corner) will not trigger desktop switching    
  
7. Automatically Disable the Top-right Corner    
In dual-corner mode, when there are only 1-2 desktops, the top-right corner is disabled. Only the top-left corner can trigger switching, and desktops are switched cyclically    
  
8. Other Settings    
Timing-related settings that usually do not need adjustment  
