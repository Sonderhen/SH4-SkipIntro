#pragma once

// SH4 Skip Intro: pressing Skip button during SH4's start-up logos goes straight to
// the main menu (no remaining logos, no opening movie).
//
// SH4's top-level modes (00611964, step stack 01236D48 / 01236D4C):
//   0 0042CCD0 boot, 1 0042CD40 logos, 2 0042CDE0 opening movie,
//   3 0042CE40 main menu, ...
// Mode 1 calls the logos task 00519160 every frame from 0042CD6A; when the
// logos end SH4 sets the gamma ramp (005212E0(01091AEC)) and goes to mode 2
// (movie), which then enters mode 3 with 00527A50(2).
//
// The call at 0042CD6A is redirected here. While the logos are on screen
// (task step 2) a SkipDemo press (input manager 006E8EA4, pressed mask +0x34,
// bit 0x200) does what SH4 does at the end of the opening: logos memory freed
// (00552050), gamma ramp set, screen black (00565CA0(0x80)), mode 3 entered
// with 00527A50(2).

// Installs the hook. False if the call site is not the expected one.
bool InstallSkipIntro();
