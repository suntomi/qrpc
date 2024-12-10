const {Builder, By, until} = require('selenium-webdriver');

const browsers = [
  "chrome",
  "safari",
  // "firefox",
  // "edge",
];
async function main() {
  const results = {};
  for (const idx in browsers) {
    const b = browsers[idx];
    console.log("test for " + b);
    let driver = new Builder().forBrowser(b).build();
    driver.get('http://localhost:8888');
    await driver.wait(until.elementLocated(By.id('result')), 200000)
      .then(async (element) => {
        const text = await element.getText();
        if (text !== "success") {
          results[b] = text;
        } else {
          console.log("test success for " + b);
        }
      })
      .finally(() => {
        driver.quit();
      });
  }
  let success = true;
  for (const name in results) {
    console.log("failed for " + name + ": " + results[name]);
    success = false;
  }
  process.exit(success ? 0 : 1);
}

main();