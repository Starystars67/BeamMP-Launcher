import 'package:flutter/material.dart';

/// The BeamMP look, taken from the website and the accounts site: the shadcn
/// grays (Tailwind gray) for surfaces and text, BeamMP orange as the accent,
/// blue for links and information, green for "good", 10 px corners.
///
/// Orange `#F36D24` is the brand colour. White text on it is only about 3:1,
/// so light mode fills buttons with the darker `#B84E14` (white text 5:1) and
/// dark mode keeps the brand orange with dark text (7:1).
abstract final class BeamMpColors {
  static const Color orange = Color(0xFFF36D24);
  static const Color orangeHover = Color(0xFFC7571A);
  static const Color orangeOnLight = Color(0xFFB84E14);
  static const Color blue = Color(0xFF4470B6);
  static const Color blueOnDark = Color(0xFF7FA2DE);
  static const Color green = Color(0xFF1D9749);

  // Tailwind gray, the palette of the shadcn variables on both sites.
  static const Color gray50 = Color(0xFFF9FAFB);
  static const Color gray100 = Color(0xFFF3F4F6);
  static const Color gray200 = Color(0xFFE5E7EB);
  static const Color gray400 = Color(0xFF99A1AF);
  static const Color gray500 = Color(0xFF6A7282);
  static const Color gray600 = Color(0xFF4A5565);
  static const Color gray800 = Color(0xFF1E2939);
  static const Color gray900 = Color(0xFF101828);
  static const Color gray950 = Color(0xFF030712);
}

/// Corner radius of the sites (`--radius: 0.625rem`).
const double beamMpRadius = 10;

const ColorScheme _light = ColorScheme(
  brightness: Brightness.light,
  primary: BeamMpColors.orangeOnLight,
  onPrimary: Colors.white,
  primaryContainer: Color(0xFFFFE8DA),
  onPrimaryContainer: Color(0xFF4A1C06),
  secondary: BeamMpColors.blue,
  onSecondary: Colors.white,
  secondaryContainer: Color(0xFFE3EBF8),
  onSecondaryContainer: Color(0xFF17294A),
  tertiary: BeamMpColors.green,
  onTertiary: Colors.white,
  tertiaryContainer: Color(0xFFD8F2E1),
  onTertiaryContainer: Color(0xFF0A3A1B),
  error: Color(0xFFE7000B),
  onError: Colors.white,
  errorContainer: Color(0xFFFEE2E2),
  onErrorContainer: Color(0xFF7A0006),
  surface: Colors.white,
  onSurface: BeamMpColors.gray950,
  onSurfaceVariant: BeamMpColors.gray600,
  surfaceContainerLowest: Colors.white,
  surfaceContainerLow: BeamMpColors.gray50,
  surfaceContainer: BeamMpColors.gray100,
  surfaceContainerHigh: BeamMpColors.gray200,
  surfaceContainerHighest: BeamMpColors.gray200,
  outline: BeamMpColors.gray500,
  outlineVariant: BeamMpColors.gray200,
  shadow: Colors.black,
  scrim: Colors.black,
  inverseSurface: BeamMpColors.gray900,
  onInverseSurface: BeamMpColors.gray50,
  inversePrimary: BeamMpColors.orange,
  surfaceTint: Colors.transparent,
);

const ColorScheme _dark = ColorScheme(
  brightness: Brightness.dark,
  primary: BeamMpColors.orange,
  onPrimary: Color(0xFF1A0A02),
  primaryContainer: Color(0xFF4A1C06),
  onPrimaryContainer: Color(0xFFFFD9C2),
  secondary: BeamMpColors.blueOnDark,
  onSecondary: Color(0xFF0B1A33),
  secondaryContainer: Color(0xFF1F3355),
  onSecondaryContainer: Color(0xFFD5E3FA),
  tertiary: BeamMpColors.green,
  onTertiary: Colors.white,
  tertiaryContainer: Color(0xFF0F3A20),
  onTertiaryContainer: Color(0xFFBFEBCD),
  error: Color(0xFFFF6467),
  onError: Color(0xFF3B0004),
  errorContainer: Color(0xFF5A0A0E),
  onErrorContainer: Color(0xFFFFD6D7),
  surface: BeamMpColors.gray950,
  onSurface: BeamMpColors.gray50,
  onSurfaceVariant: BeamMpColors.gray400,
  surfaceContainerLowest: BeamMpColors.gray950,
  surfaceContainerLow: Color(0xFF0A111F),
  surfaceContainer: BeamMpColors.gray900,
  surfaceContainerHigh: BeamMpColors.gray800,
  surfaceContainerHighest: Color(0xFF2A3648),
  outline: BeamMpColors.gray500,
  outlineVariant: Color(0x1AFFFFFF), // white at 10 %, the sites' dark border
  shadow: Colors.black,
  scrim: Colors.black,
  inverseSurface: BeamMpColors.gray50,
  onInverseSurface: BeamMpColors.gray900,
  inversePrimary: BeamMpColors.orangeOnLight,
  surfaceTint: Colors.transparent,
);

ThemeData buildTheme(Brightness brightness) {
  final scheme = brightness == Brightness.dark ? _dark : _light;
  final shape = RoundedRectangleBorder(
    borderRadius: BorderRadius.circular(beamMpRadius - 2),
  );
  final base = ThemeData(
    colorScheme: scheme,
    useMaterial3: true,
    visualDensity: VisualDensity.standard,
  );
  final text = base.textTheme;
  // dark cards sit on a lighter gray than the page, like the sites' --card
  final cardColor = brightness == Brightness.dark
      ? scheme.surfaceContainer
      : scheme.surface;
  return base.copyWith(
    scaffoldBackgroundColor: scheme.surface,
    textTheme: text.copyWith(
      headlineSmall: text.headlineSmall?.copyWith(fontWeight: FontWeight.w600),
      titleLarge: text.titleLarge?.copyWith(fontWeight: FontWeight.w600),
      titleMedium: text.titleMedium?.copyWith(fontWeight: FontWeight.w600),
      titleSmall: text.titleSmall?.copyWith(fontWeight: FontWeight.w600),
    ),
    appBarTheme: AppBarTheme(
      backgroundColor: scheme.surface,
      foregroundColor: scheme.onSurface,
      surfaceTintColor: Colors.transparent,
      elevation: 0,
      scrolledUnderElevation: 0,
      centerTitle: false,
      titleTextStyle: text.titleLarge?.copyWith(
        fontWeight: FontWeight.w600,
        color: scheme.onSurface,
      ),
      // like the sites' header: a hairline underneath
      shape: Border(bottom: BorderSide(color: scheme.outlineVariant)),
    ),
    navigationRailTheme: NavigationRailThemeData(
      backgroundColor: scheme.surfaceContainerLow,
      indicatorColor: scheme.primaryContainer,
      indicatorShape: shape,
      selectedIconTheme: IconThemeData(color: scheme.primary),
      unselectedIconTheme: IconThemeData(color: scheme.onSurfaceVariant),
      selectedLabelTextStyle: text.labelMedium?.copyWith(
        color: scheme.onSurface,
        fontWeight: FontWeight.w600,
      ),
      unselectedLabelTextStyle: text.labelMedium?.copyWith(
        color: scheme.onSurfaceVariant,
      ),
    ),
    dividerTheme: DividerThemeData(color: scheme.outlineVariant, space: 1),
    cardTheme: CardThemeData(
      elevation: 0,
      color: cardColor,
      surfaceTintColor: Colors.transparent,
      margin: EdgeInsets.zero,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(beamMpRadius),
        side: BorderSide(color: scheme.outlineVariant),
      ),
    ),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        shape: shape,
        minimumSize: const Size(0, 40),
        padding: const EdgeInsets.symmetric(horizontal: 20),
        textStyle: text.labelLarge?.copyWith(fontWeight: FontWeight.w600),
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        shape: shape,
        minimumSize: const Size(0, 40),
        padding: const EdgeInsets.symmetric(horizontal: 20),
        side: BorderSide(color: scheme.outline),
        textStyle: text.labelLarge?.copyWith(fontWeight: FontWeight.w600),
      ),
    ),
    textButtonTheme: TextButtonThemeData(
      style: TextButton.styleFrom(
        shape: shape,
        textStyle: text.labelLarge?.copyWith(fontWeight: FontWeight.w600),
      ),
    ),
    inputDecorationTheme: InputDecorationThemeData(
      border: OutlineInputBorder(
        borderRadius: BorderRadius.circular(beamMpRadius - 2),
        borderSide: BorderSide(color: scheme.outline),
      ),
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(beamMpRadius - 2),
        borderSide: BorderSide(color: scheme.outline),
      ),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(beamMpRadius - 2),
        borderSide: BorderSide(color: scheme.primary, width: 2),
      ),
      errorBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(beamMpRadius - 2),
        borderSide: BorderSide(color: scheme.error),
      ),
      focusedErrorBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(beamMpRadius - 2),
        borderSide: BorderSide(color: scheme.error, width: 2),
      ),
    ),
    switchTheme: SwitchThemeData(
      trackOutlineColor: WidgetStateProperty.resolveWith(
        (states) => states.contains(WidgetState.selected)
            ? Colors.transparent
            : scheme.outline,
      ),
    ),
    snackBarTheme: SnackBarThemeData(
      behavior: SnackBarBehavior.floating,
      shape: shape,
    ),
    dialogTheme: DialogThemeData(
      backgroundColor: cardColor,
      surfaceTintColor: Colors.transparent,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(beamMpRadius),
        side: BorderSide(color: scheme.outlineVariant),
      ),
    ),
    listTileTheme: const ListTileThemeData(),
  );
}
