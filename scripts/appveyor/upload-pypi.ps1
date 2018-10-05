trap { Write-Error $_; Exit 1 }
python -m pip install --disable-pip-version-check twine
python -m twine --verbose --repository-url  https://test.pypi.org/legacy/ dist/*
