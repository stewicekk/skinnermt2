# Skill: Professional Theme System for RigApp

## Description
Create a world-class dark/blue/light theme system with smooth animations for the WPF application.

## File: C:\rigapp\RigApp\Theme.xaml (enhance existing)

## Requirements:

### Color Palette (Dark Theme - Primary):
```xml
<!-- Backgrounds -->
<Color x:Key="BgPrimary">#1E1E2E</Color>      /* Main window */
<Color x:Key="BgSecondary">#252536</Color>    /* Panels, expanders */
<Color x:Key="BgTertiary">#2D2D44</Color>     /* Inputs, lists */
<Color x:Key="BgHover">#3A3A5A</Color>        /* Hover states */
<Color x:Key="BgPressed">#45456A</Color>      /* Pressed states */

<!-- Accents -->
<Color x:Key="AccentBlue">#00D4FF</Color>     /* Primary accent */
<Color x:Key="AccentPurple">#B48CFF</Color>   /* Secondary */
<Color x:Key="AccentGreen">#00FF88</Color>   /* Success */
<Color x:Key="AccentOrange">#FFB800</Color>  /* Warning */
<Color x:Key="AccentRed">#FF4757</Color>     /* Error */

<!-- Text -->
<Color x:Key="TextPrimary">#FFFFFF</Color>
<Color x:Key="TextSecondary">#B8B8D0</Color>
<Color x:Key="TextMuted">#787890</Color>
<Color x:Key="TextDisabled">#484860</Color>

<!-- Borders -->
<Color x:Key="BorderDefault">#3A3A5A</Color>
<Color x:Key="BorderFocus">#00D4FF</Color>
<Color x:Key="BorderError">#FF4757</Color>
```

### Animations (all 200-300ms easing):
- Window fade-in (0.3s)
- Expander expand/collapse (0.2s)
- Button hover/press (0.1s color, 0.15s scale)
- ListBoxItem selection glow (0.2s)
- ProgressBar pulse (1.5s infinite)
- Status indicator blink (0.5s × 3 on error)
- Tooltip fade (0.15s)

### Control Templates:
- **Button**: Rounded corners (4px), ripple effect on click, icon + text
- **ToggleButton**: Animated check mark, color transition
- **Expander**: Chevron rotation (0.2s), content slide (0.25s)
- **TextBox**: Floating label, focus ring animation
- **ComboBox**: Dropdown slide + fade
- **Slider**: Thumb glow on hover, track color
- **ProgressBar**: Gradient fill, pulse animation
- **TreeViewItem**: Indent lines, selection highlight with glow
- **TabControl**: Tab slide indicator, content cross-fade
- **DataGrid**: Row hover, selection, alternating rows

### Custom Controls:
- **StatusIndicator**: Colored dot + text, blink animation
- **WeightHeatmapLegend**: Gradient bar with value labels
- **BoneTreeItem**: Visibility eye, lock icon, weight badge
- **ViewportToolbar**: Icon buttons with tooltips

### Theme Switching:
- Runtime theme change (Dark/Blue/Light)
- Persist in SettingsManager
- Smooth cross-fade (0.3s) between themes

## Verification:
- All existing UI uses theme resources
- No hardcoded colors in XAML
- Smooth animations at 60fps
- Theme switch instant