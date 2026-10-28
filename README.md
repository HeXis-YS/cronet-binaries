# Cronet Automation

This orphan branch contains the automation that maintains the Chromium fork.
Chromium source lives on versioned `hray/<WebView version>` branches in the
same repository. The release baseline is the Stable WebView entry returned by
ChromiumDash. The release version selects the corresponding Chromium tag.

The weekly workflow currently records the newest WebView release. Rebase and
platform builds will run after the native adapter has been migrated into the
versioned Chromium branch. A failed rebase remains a failed workflow; the
automation does not resolve conflicts or modify an existing release branch.

`hray-core` consumes the stable C ABI from the resulting `libcronet.so`; a
Chromium update does not require an hray-core update when that ABI is unchanged.
