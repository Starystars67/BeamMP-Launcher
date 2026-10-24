import 'package:flutter/material.dart';

/// The BeamMP logo (truck and wordmark), black or white to suit the theme.
/// The files are listed so tests can load them before a screenshot.
const List<String> beamMpLogoAssets = [
  'assets/brand/beammp_on_light.png',
  'assets/brand/beammp_on_dark.png',
];

class BeamMpLogo extends StatelessWidget {
  const BeamMpLogo({this.width = 160, super.key});

  final double width;

  @override
  Widget build(BuildContext context) {
    final dark = Theme.of(context).brightness == Brightness.dark;
    return Image.asset(
      dark
          ? 'assets/brand/beammp_on_dark.png'
          : 'assets/brand/beammp_on_light.png',
      width: width,
      filterQuality: FilterQuality.high,
      semanticLabel: 'BeamMP',
    );
  }
}
