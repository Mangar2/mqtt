module.exports = [
    {
        'description': 'Matches a/b/c',
        'pattern': 'a/b/c',
        'tests': [
            { 'description':'a/b/c', 'topic': 'a/b/c', 'expected': 0 },
            { 'description':'a/b/', 'topic': 'a/b/', 'expected': undefined }
        ]
    },
    {
        'description': 'Matches anything',
        'pattern': '#',
        'tests': [
            { 'description':'any', 'topic': 'any', 'expected': 0 },
            { 'description':'not $any', 'topic': '$any', 'expected': undefined }
        ]
    },
    {
        'description': 'Matches +',
        'pattern': '+/b/+',
        'tests': [
            { 'description':'a/b/c', 'topic': 'a/b/c', 'expected': 0 },
            { 'description':'/a/b/c', 'topic': '/a/b/c', 'expected': undefined }
        ]
    },
    {
        'description': 'Two pattern',
        'pattern': {'a/b/c': 1, '+/b/c': 0},
        'tests': [
            { 'description':'a/b/c', 'topic': 'a/b/c', 'expected': 1 },
            { 'description':'b/b/c', 'topic': 'b/b/c', 'expected': 0 }
        ]
    }
]