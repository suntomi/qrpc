export function promisify(a) {
    if (a instanceof Promise) {
        return a;
    }
    else {
        return Promise.resolve(a);
    }
}
//# sourceMappingURL=types.js.map