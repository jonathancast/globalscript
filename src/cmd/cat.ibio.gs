#! /home/jcast/globalscript/bin/ibio

for ('as ← env.arguments) analyze as
    case nil. cat
    (case _. foreachM as $ λ 'a. for ('e ← file.open o{r} $ file.name.read a) analyze e
        case left 'e. err.to (send qq{§a: cannot open: §e\n}) *> abend e
        case right 'if. cat << if
    )
(where
    'cat = for ('s ← receive $ many symbol) send s;
    'many = λ 'a. for rec ('m = unit [] <|> (:) <$> a <*> m) m;
    'foreachM = λ 'xn 'f. sequence (map f xn);
    'map = λ 'f. foldr ((:) · f) [];
    'sequence = foldr (λ 'a 'as. (:) <$> a <*> as) (unit []);
)
