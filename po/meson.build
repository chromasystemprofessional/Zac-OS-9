# The message catalogs (docs/i18n.md). zacos9-pot rewrites the template
# from the strings marked in POTFILES.in; a language listed in LINGUAS
# installs its catalog to <prefix>/share/locale, where gettext finds it.
i18n = import('i18n')
i18n.gettext('zacos9',
  args: [
    '--from-code=UTF-8',
    '--add-comments=TRANSLATORS:',
    '--keyword=_',
    '--keyword=N_',
    '--keyword=tr9',
    '--msgid-bugs-address=https://github.com/chromasystemprofessional/Zac-OS-9/issues',
    '--copyright-holder=ZacOS 9 contributors',
  ])
