# Utility app layout previews

These images render the app XML with the shipped font and sample runtime data.
They show the proposed layout, not a captured Dreamcast session.

- [Overview](utility-apps-preview.png)
- [Launcher icons](icons.png)
- [File Manager](file-manager.png)
- [GD Play](gd-play.png)
- Settings: [Display](settings-0.png), [Sound](settings-1.png),
  [Startup](settings-2.png), [Clock](settings-3.png), [System](settings-4.png)

Regenerate from the repository root:

```sh
python3 utils/preview_utility_apps.py docs/utility-apps-preview
```

The separate `codex/utility-apps` workflow produces a test update artifact.
Full release publication requires separate approval after hardware testing.
See [controls and validation notes](../../utils/README.utility-apps.md).
