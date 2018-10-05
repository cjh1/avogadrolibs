trap { Write-Error $_; Exit 1 }
python -m pip install --disable-pip-version-check twine
Write-Host "installed"
#python -m twine upload --repository-url  https://test.pypi.org/legacy/ dist/*
python -m twine --verbose --repository-url  https://test.pypi.org/legacy/ dist/*
