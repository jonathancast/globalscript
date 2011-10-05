module 'gslex.(
    gssequence = 'sequence,
    ...
) ∝ 〈
    sig gssequence :: ∀ 's 'α 'β. parser s α → parser s β → parser s [α];
    'gssequence
        = (λ 'p0 'p1.
            (λ 'x. [x]) <$> p0
        <|> many (p0 <* p1)
      ) = (λ 'p0 'p1.
                return []
            <|> for ('x ← p0)
                  return [x] <|> for (
                    'xn ← p1 *> many (p0 <* p1)
                  ) return (x:xn)
      )
    ;
〉
(where
)
