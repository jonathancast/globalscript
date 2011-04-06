test.data %ch channel (return 〈〉) $
    test.run (λ 'self. hello >> #ch self) $
        ibio.test.contains ch qq{Hello, World!\n}
where
    'hello = print qq{Hello, World!\n}
