#! /home/jcast/globalscript/iotest

for (
    'c ← channel;
    'test.case ← new $ was.run c;
    _ ← #run test.case `finally` #teardown test.case;
    _ ← print [false] >> c;
    's ← parse symbol << c;
) (analyze s
    case false. print qq{Failed} *> throw {failed}
    case true. return 〈〉
)
(where
    'test.case = λ 'c.
        method %run (λ 'self. return 〈〉) $
        method %teardown (λ 'self. return 〈〉) $
        class.empty
    ;
)
