const {Builder, By, until} = require('selenium-webdriver');

const browsers = [
  "chrome",
  "safari",
  "firefox",
  "edge",
];
for (const b in browsers) {
  console.log("test for " + b);
  let driver = new Builder().forBrowser(b).build();  // あるいはあなたが使用しているブラウザに合わせて変更
  driver.get('http://localhost:8888');

  driver.wait(until.elementLocated(By.id('result')), 10000)  // あなたが待つ要素のIDに合わせて変更
    .then(element => {
      // ここでelementを使用する処理を書くことができます
      if (element.innerHTML !== "success") {
        console.log("failed for " + b + ": result = " + element.innerHTML);
        driver.quit();
        process.exit(1);
      }
    })
    .finally(() => {
      driver.quit();
    });
}
