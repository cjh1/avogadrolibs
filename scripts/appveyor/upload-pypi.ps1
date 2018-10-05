trap { Write-Error $_; Exit 1 }

pip install --disable-pip-version-check --user --upgrade pip
pip install --user twine
$Env:path = $env:path + ";C:\Users\appveyor\AppData\Roaming\Python\Scripts\"
twine upload --repository-url  https://test.pypi.org/legacy/ dist/*
