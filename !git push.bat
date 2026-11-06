:: 1. Navigate to your project folder
cd Q:\morphosis\code\s3\S3_jam

:: 2. Initialize a brand new .git folder
git init

:: 3. Add your remote
git remote add origin https://github.com/bitseq27/s3jam.git
:: 4. Stage and commit everything
git add .
git commit -m "Fresh start - rebuilt git history"

:: 5. Force push (THIS OVERWRITES EVERYTHING ON GITHUB)
git branch -M main
git push -f origin main

Pause