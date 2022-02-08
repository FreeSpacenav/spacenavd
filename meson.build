project('src', 'c',
  version : run_command(['git', 'describe', '--tags'], capture : true, check : true).stdout().strip(),
  default_options : ['warning_level=3'])

subdir('src')
