# Q UI Style Guide

## Visual Identity

- **Dark** design, purple-centric palette
- **Glassmorphism**: backdrop-blur + transparency throughout
- **Rounded**: `rounded-xl` containers, `rounded-full` buttons/badges
- **Smooth**: 200-300ms transitions, ease-in-out
- **Subtle borders**: often at 10% opacity
- **Font**: DM Sans

---

## Colors

### Brand Purple

| Token | Hex |
|-------|-----|
| `primary-purple-90` | `#6B26FF` (primary accent) |
| `primary-purple-100` | `#8D72D9` |
| `primary-purple-80` | `#AF8BFF` |
| `primary-purple-400` | `#7459C1` |
| `primary-purple-active` | `#624FA6` |
| `primary-purple-200` | `#241B3B` |
| `primary-purple-300` | `#110A23` |
| `primary-purple-500` | `#120831` |

### Backgrounds

| Token | Hex |
|-------|-----|
| `primary-background` | `#151319` |
| `primary-black-100` | `#0F091C` |
| `static-dark` | `#09091F` |
| `accent-gray-background` | `#232338` |

### Grays

| Token | Hex |
|-------|-----|
| `primary-gray-80` | `#434242` |
| `primary-gray-90` | `#2D2C2C` |
| `secondary-gray-100` | `#A7A2B6` |
| `secondary-gray-200` | `#605979` |
| `secondary-gray-300` | `#282436` |
| `accent-gray` | `#3A3A4C` |

### Status

| Token | Hex |
|-------|-----|
| `status-green` | `#A0D4B1` |
| `status-orange` / `primary-yellow-100` | `#F2A359` |
| `status-red` | `#E24B32` |
| `primary-red-100` | `#F25C5C` |
| `primary-green-90` | `#19D95D` |

### Glass / Transparency

```
effects-glass-dark:    rgba(9, 9, 31, 0.50)
effects-glass-dark-30: rgba(45, 44, 44, 0.3)
```

### Semantic Tokens (CSS variables)

| Token | Value |
|-------|-------|
| `--bg-default` | `#09091F` |
| `--bg-primary` | `#232338` |
| `--bg-secondary` | `#3A3A4C` |
| `--bg-accent` | `#6B26FF` |
| `--border-default` | `#222235` |
| `--text-default` | `#E6E6E9` |
| `--icon-default` | `#E6E6E9` |

---

## Typography

### Font

**DM Sans** (Google Fonts), fallback: Tahoma, sans-serif

### Scale (qfont-*)

| Token | Size | Line Height |
|-------|------|-------------|
| `qfont-1` | 12px | 16px |
| `qfont-2` | 13px | 20px |
| `qfont-3` | 15px | 21px |
| `qfont-4` | 16px | 26px |
| `qfont-5` | 17px | 26px |
| `qfont-5-5` | 20px | 26px |
| `qfont-6` | 24px | 31px |
| `qfont-7` | 26px | 34px |
| `qfont-8` | 32px | 40px |
| `qfont-9` | 48px | 58px |
| `qfont-10` | 56px | 67px |

### Semantic Classes

| Class | Size | Weight |
|-------|------|--------|
| `.qfont-title-xl` | 36px/43px | 700 |
| `.qfont-title-l` | 24px/30px | 700 |
| `.qfont-title-xs` | 20px/24px | 700 |
| `.qfont-heading-lg` | 32px/38px | 700 |
| `.qfont-heading-md` | 28px/34px | 700 |
| `.qfont-heading-sm` | 24px/29px | 500 |
| `.qfont-label-lg` | 18px/22px | 500 |
| `.qfont-label-md` | 16px/19px | 400 |
| `.qfont-label-sm` | 14px/17px | 400 |
| `.qfont-body-lg` | 18px/27px | 400 |
| `.qfont-normal` | 16px/24px | 400 |
| `.qfont-normal-xs` | 14px/17px | 400 |

---

## Spacing

### Common Patterns

- **Container padding**: `p-4`, `p-6`, `p-8`
- **Button padding**: sm `px-4 py-2.5`, md/lg `px-6 py-3.5`, xl `px-6 py-4`
- **Input height**: `h-14`, padding `px-4`
- **Dialog padding**: `p-6` or `p-7`
- **Gaps**: small `gap-1`–`gap-3`, medium `gap-4`–`gap-6`, large `gap-8`–`gap-14`

### Fixed Sizes

- **Header height**: `80px`
- **Navbar collapsed**: `100px`
- **Navbar expanded**: `264px`
- **Side panel**: `360px`

---

## Breakpoints

| Name | Width |
|------|-------|
| `sm` | 1024px |
| `ipadPro` | 1381px |
| `md` | 1440px |
| `lg` | 2000px |

---

## Border Radius

| Usage | Value |
|-------|-------|
| Buttons, badges, avatars, pills | `rounded-full` |
| Cards, dialogs, inputs, containers | `rounded-xl` |
| Secondary elements, dropdowns, tabs | `rounded-lg` |
| Small elements | `rounded-md` |
| Major dialogs | `rounded-2xl` |

---

## Shadows

| Usage | Value |
|-------|-------|
| Subtle (tabs, small cards) | `shadow-sm` |
| Dialogs, dropdowns, cards | `shadow-lg` |
| Input focus | `0 0 0 4px #E9E4F9` |
| Input error | `0 0 0 4px #F7CEC7` |
| Navbar | `0px 4px 8px 0px #081F590F` |

### Glass Inset Shadows

```css
inset 0 1px 1px rgba(255,255,255,0.1), inset 0 -1px 1px rgba(0,0,0,0.2)
```

---

## Z-Index

| Layer | Value |
|-------|-------|
| Tooltip | 10 |
| Drawer backdrop | 50 |
| Drawer | 51 |
| Dialog overlay | 100 |
| Dialog | 101 |
| Min screen overlay | 200 |
| Toast | 9999 |

---

## Glassmorphism

Recurring pattern throughout the UI:

```css
/* Standard glass panel */
bg-effects-glass-dark backdrop-blur-md

/* Glass card */
bg-white/5 backdrop-blur-md border border-white/10

/* Overlay */
bg-primary-black-200/85 backdrop-blur-xl
```

Common blur levels: `backdrop-blur-sm`, `backdrop-blur-md`, `backdrop-blur-xl`

---

## Gradients

### Background

```css
linear-gradient(65deg,
  #0F0D11 10%, #151319 20%, #1E1B23 25%,
  #242029 28%, #151319 31%, #0A090C 37%,
  #0F0D11 43%, #151319 59%, #1E1B23 66%,
  #151319 73%, #0F0D11 82%, #151319 100%)
```

### Purple Accent

```css
linear-gradient(90deg, #6B26FF 3.85%, #AF8BFF 64.42%)
```

---

## Component Recipes

### Button

```css
rounded-full px-6 py-3.5 text-sm font-medium h-12
flex gap-2 items-center justify-center
transition-colors duration-300 ease-in-out tracking-wider
```

Sizes: `h-8` (sm), `h-10` (md), `h-12` (lg), `h-14` (xl)

### Card

```css
rounded-xl border p-6
border-primary-purple-100/10
bg-primary-purple-300/30
shadow-lg flex flex-col
```

### Dialog

```css
/* Overlay */
fixed inset-0 z-dialog-overlay
bg-primary-black-200/85 backdrop-blur-xl

/* Content */
fixed left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2
z-dialog rounded-2xl border p-6 shadow-lg max-h-[85vh]
```

### Input

```css
border rounded-xl h-14 px-4
border-primary-purple-100/10 bg-primary-purple-300/30
placeholder:text-sm
focus:ring-3 focus:ring-offset-0
```

### Tab Bar (glass)

```css
/* Container */
flex w-max rounded-xl p-1
bg-white/5 backdrop-blur-md
border border-white/10

/* Active tab indicator */
absolute top-1 bottom-1 rounded-lg
bg-white/95 shadow-sm

/* Tab item */
px-5 py-2 text-sm transition-colors duration-200
```

### Dropdown

```css
rounded-md border p-2 shadow-lg backdrop-blur-lg
border-primary-purple-100/10 bg-accent-2
max-h-[300px] overflow-y-auto

/* Items */
h-11 rounded-sm px-3
hover:bg-primary-purple-100/10
```

### Badge

```css
rounded-[1000px] border px-2 py-1
border-primary bg-primary text-default
```

---

## Animations

| Name | Duration | Use |
|------|----------|-----|
| Standard transition | 200ms ease-in-out | Most hover/focus states |
| Button transition | 300ms ease-in-out | Color changes on buttons |
| `overlayShow` | 150ms cubic-bezier(0.16,1,0.3,1) | Dialog overlay fade-in |
| `contentShow` | 150ms cubic-bezier(0.16,1,0.3,1) | Dialog content scale-in |
| `shimmer` | 2s linear infinite | Loading skeleton |
| `pulse-gradient` | 3s ease-out infinite alternate | Pulsing backgrounds |
| `bg-gradient` | 15s ease infinite | Animated background |
| `ai-glow` | 1.5s ease-in-out infinite | AI indicator glow |
| `loadingPulse` | 0.6s ease-in-out infinite | Loading bars (staggered) |
| `spin-slow` | 6s linear infinite | Slow spinner |

### Framer Motion (tabs)

```js
type: 'spring', stiffness: 400, damping: 30
```

---

## Icons

- Source: Lucide React + custom SVGs
- Sizes: `h-5 w-5` (20px), `h-7 w-7` (28px), `size-10` (40px)
- Color: `text-icon-default`, `text-icon-accent`
- Hover: `group-hover:text-white`

---

## Layout

- **Sidebar + content**: `flex h-screen w-screen flex-col sm:flex-row`
- **Full height column**: `flex flex-col h-full`
- **Centered**: `flex items-center justify-center`
- **Spaced row**: `flex items-center justify-between`
- **Scrollable**: `overflow-y-auto scroll-smooth` with `hide-scrollbar`
- **Flex-based** layouts preferred over grid
