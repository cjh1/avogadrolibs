trap { Write-Error $_; Exit 1 }

python -m pip install -U pip
python -m pip install twine
python -m twine upload --repository-url  https://test.pypi.org/legacy/ dist/*
