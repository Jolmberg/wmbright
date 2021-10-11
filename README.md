# wmbright

A brightness control dockapp in the style of wmix. Allows control of backlight
level for some laptop monitors and gamma manipulation for most outputs.

## Usage

Upon starting, wmbright detects all active outputs and tries to determine the
best way to control their brightness. Backlight control is used if available,
with gamma manipulation as a fallback. There are multiple ways to control
brightness with wmbright:

 1. Click and drag on the knob
 2. Use the mouse wheel anywhere inside the dockapp
 3. Use the standard brightness keys if available on your keyboard
 4. Send the signals SIGUSR1 and SIGUSR2

Initially, wmbright is set to the "ALL" output which means that any
brightness change is applied to all available outputs simultaneously. Use
the arrow buttons on the bottom left to select individual outputs instead.

Above the arrow buttons are indicators for the different brightness control
methods. Typically "BL", i.e., backlight, is superior as it will actually
control the backlight of your monitor rather than just changing the colours
of the pixels, but if you prefer gamma manipulation, click on the gamma
indicator to switch to the gamma method. When wmbright is set to the "ALL"
output, clicking the the indicators will (try to) switch the method of all
outputs at once.

## Configuration file

wmbright will read configuration values from a .wmbrightrc file in your
home directory. The values understood are the following (these are also
the defaults, in case no configuration file is found):

    # 1 = yes, 0 = no
    
    mousewheel=1            # use mousewheel?
    scrolltext=1            # scroll output names when they don't fit
    osd=1                   # display OSD?
    osdcolor=green          # color of the OSD (from rgb.txt)
    wheelbtn1=4             # which mouse button is "wheel up"
    wheelbtn2=5             # which mouse button is "wheel down"
    wheelstep=3             # the step for mouse wheel adjustment

Additionally, an exclude parameter is understood, allowing outputs to be
excluded from wmbright control:

    exclude=HDMI-0
    exclude=DVI-I-1

A sample configuration file is provided in sample.wmbrightrc.

## Command line parameters

Run wmbright -h to list the command line parameters.

## Troubleshooting

# Xorg config

wmbright uses libxrandr to manipulate backlight as well as gamma. Sometimes
xrandr cannot access the backlight property out of the box. This can be fixed
by adding a small piece of xorg config. If your system contains the directory
/sys/class/backlight/intel_backlight but backlight manipulation does not work,
it is likely that this can be remedied by adding the file
/etc/X11/xorg.conf.d/20-video.conf

    Section "Device"
        Identifier  "Intel Graphics"
        Driver      "intel"
        Option      "Backlight"  "intel_backlight"
    EndSection

(courtesy of the Arch Wiki, https://wiki.archlinux.org/title/backlight)

# Night light

Night light applications such as redshift typically use the same gamma controls
as wmbright. If your brightness seems to revert shortly after adjusting it with
wmbright, it's likely that you have some sort of night light software running.
There is not much that can be done about this outside of disabling the night
light app.
