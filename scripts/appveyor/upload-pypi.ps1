trap { Write-Error $_; Exit 1 }

$Env:path = $env:path + ";C:\Users\appveyor\AppData\Roaming\Python\Scripts\"
pip install --disable-pip-version-check --user --upgrade pip
pip install --user twine
twine upload --repository-url  https://test.pypi.org/legacy/ dist/*
